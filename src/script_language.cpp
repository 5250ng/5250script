// 5250ng - A modern IBM TN5250 terminal emulator
// Copyright (C) 2025-2026 Remi GASCOU (Podalirius)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include "script_language.h"
#include "script_lexer.h"
#include <QMap>
#include <QRegularExpression>
#include <algorithm>

namespace core::scripting::language {

TokenCategory categoryOf(TokenType type) {
    switch (type) {
    case TokenType::TYPE: case TokenType::PRESS: case TokenType::MOVE:
    case TokenType::EXPECT: case TokenType::EXTRACT: case TokenType::WAIT:
    case TokenType::GLOBAL: case TokenType::SET: case TokenType::INC:
    case TokenType::DEC: case TokenType::ADD: case TokenType::ABORT:
    case TokenType::LOG: case TokenType::PAUSE: case TokenType::INPUT:
        return TokenCategory::Command;

    case TokenType::IF: case TokenType::ELSE: case TokenType::ENDIF:
    case TokenType::WHILE: case TokenType::ENDWHILE: case TokenType::REPEAT:
    case TokenType::ENDREPEAT: case TokenType::LABEL: case TokenType::GOTO:
    case TokenType::THEN: case TokenType::DEF: case TokenType::ENDDEF:
    case TokenType::CALL: case TokenType::RETURN: case TokenType::ON:
        return TokenCategory::Control;

    case TokenType::ENTER: case TokenType::F1: case TokenType::F2: case TokenType::F3:
    case TokenType::F4: case TokenType::F5: case TokenType::F6: case TokenType::F7:
    case TokenType::F8: case TokenType::F9: case TokenType::F10: case TokenType::F11:
    case TokenType::F12: case TokenType::F13: case TokenType::F14: case TokenType::F15:
    case TokenType::F16: case TokenType::F17: case TokenType::F18: case TokenType::F19:
    case TokenType::F20: case TokenType::F21: case TokenType::F22: case TokenType::F23:
    case TokenType::F24: case TokenType::PAGEUP: case TokenType::PAGEDOWN:
    case TokenType::ATTN: case TokenType::SYSREQ: case TokenType::HELP:
    case TokenType::CLEAR: case TokenType::PRINT:
        return TokenCategory::AidKey;

    case TokenType::TAB: case TokenType::BACKTAB: case TokenType::BACKSPACE:
    case TokenType::DELETE_KEY: case TokenType::INSERT: case TokenType::HOME:
    case TokenType::END: case TokenType::ESC: case TokenType::FIELDPLUS:
    case TokenType::FIELDMINUS: case TokenType::FIELDEXIT: case TokenType::DUP:
    case TokenType::ERASEINPUT: case TokenType::ERASEFIELD: case TokenType::ERASEEOF:
        return TokenCategory::LocalKey;

    case TokenType::KEY_CHAR:
        return TokenCategory::CharKey;

    case TokenType::TEXT: case TokenType::AT: case TokenType::ROW:
    case TokenType::CURSOR: case TokenType::KEYBOARD: case TokenType::UNLOCKED:
    case TokenType::ERRORLOCKED: case TokenType::FIELD: case TokenType::CONTAINS:
    case TokenType::MESSAGEWAITING: case TokenType::NOT: case TokenType::LENGTH:
    case TokenType::FROM: case TokenType::ERROR: case TokenType::TIMEOUT:
    case TokenType::INPUTFIELD: case TokenType::NEXT: case TokenType::PREVIOUS:
    case TokenType::COL: case TokenType::LINE: case TokenType::UP:
    case TokenType::DOWN: case TokenType::LEFT: case TokenType::RIGHT:
    case TokenType::DELAY: case TokenType::JITTER: case TokenType::EXPECT_TIMEOUT:
    case TokenType::ISSET:
        return TokenCategory::SubKeyword;

    case TokenType::OP_EQ: case TokenType::OP_NE: case TokenType::OP_LT:
    case TokenType::OP_GT: case TokenType::OP_LE: case TokenType::OP_GE:
    case TokenType::AND: case TokenType::OR:
        return TokenCategory::Operator;

    case TokenType::LPAREN: case TokenType::COMMA: case TokenType::RPAREN:
        return TokenCategory::Punctuation;

    case TokenType::NUMBER_LITERAL: return TokenCategory::Number;
    case TokenType::STRING_LITERAL: return TokenCategory::String;
    case TokenType::VARIABLE:       return TokenCategory::Variable;

    case TokenType::EOL: case TokenType::EOF_TOKEN: case TokenType::UNKNOWN:
        return TokenCategory::Unknown;
    }
    return TokenCategory::Unknown;
}

QVector<TokenCategory> classifyLine(const TokenLine &tokens, const QString &lineText) {
    QVector<TokenCategory> result;
    result.reserve(tokens.size());
    for (int i = 0; i < tokens.size(); ++i) {
        const ScriptToken &tok = tokens[i];
        TokenCategory cat = categoryOf(tok.type);
        if (tok.type == TokenType::STRING_LITERAL) {
            const int start = tok.column - 1;
            const bool quoted = start >= 0 && start < lineText.length() && lineText[start] == '"';
            const TokenType prev = i > 0 ? tokens[i - 1].type : TokenType::EOF_TOKEN;
            const TokenType next = i + 1 < tokens.size() ? tokens[i + 1].type : TokenType::EOF_TOKEN;
            if (quoted || prev == TokenType::LOG) {
                cat = TokenCategory::String;
            } else if (prev == TokenType::DEF || prev == TokenType::CALL || next == TokenType::LPAREN) {
                cat = TokenCategory::Function;
            } else if (prev == TokenType::LABEL || prev == TokenType::GOTO) {
                cat = TokenCategory::Label;
            } else {
                cat = TokenCategory::Identifier;
            }
        }
        result.append(cat);
    }
    return result;
}

QStringList keywords(TokenCategory category) {
    static const QMap<TokenCategory, QStringList> byCategory = [] {
        // token type -> shortest spelling, ordered by declaration order
        QMap<int, QString> canonical;
        ScriptLexer lexer;
        const auto &table = lexer.keywords();
        for (auto it = table.constBegin(); it != table.constEnd(); ++it) {
            const int key = static_cast<int>(it.value());
            auto existing = canonical.find(key);
            if (existing == canonical.end() || it.key().length() < existing.value().length())
                canonical[key] = it.key();
        }
        QMap<TokenCategory, QStringList> map;
        for (auto it = canonical.constBegin(); it != canonical.constEnd(); ++it)
            map[categoryOf(static_cast<TokenType>(it.key()))].append(it.value());
        map[TokenCategory::CharKey] = ScriptLexer::keyCharNames();
        return map;
    }();
    return byCategory.value(category);
}

QStringList statementKeywords() {
    QStringList result = keywords(TokenCategory::Command);
    for (const QString &kw : keywords(TokenCategory::Control)) {
        if (kw != QLatin1String("THEN"))
            result.append(kw);
    }
    return result;
}

QStringList aidKeys() { return keywords(TokenCategory::AidKey); }
QStringList localKeys() { return keywords(TokenCategory::LocalKey); }
QStringList charKeys() { return keywords(TokenCategory::CharKey); }

QStringList builtinVariables() {
    // Keep in sync with ScriptExecutor (constructor, REPEAT/EXPECT handling
    // and updateBuiltinVariables())
    return {
        QStringLiteral("$SCREEN_ROWS"), QStringLiteral("$SCREEN_COLS"),
        QStringLiteral("$CURSOR_ROW"), QStringLiteral("$CURSOR_COL"),
        QStringLiteral("$KEYBOARD_STATE"), QStringLiteral("$MESSAGE_WAITING"),
        QStringLiteral("$EXPECT_RESULT"), QStringLiteral("$REPEAT_INDEX"),
    };
}

int commentStart(const QString &lineText) {
    bool inString = false;
    for (int i = 0; i < lineText.length(); ++i) {
        const QChar c = lineText[i];
        if (inString) {
            if (c == '\\' && i + 1 < lineText.length())
                ++i;
            else if (c == '"')
                inString = false;
        } else if (c == '"') {
            inString = true;
        } else if (c == '#') {
            return i;
        }
    }
    return -1;
}

bool isMetadataComment(const QString &lineText) {
    static const QRegularExpression re(QStringLiteral("^\\s*#\\s*@[\\w.]+\\s*="));
    return re.match(lineText).hasMatch();
}

} // namespace core::scripting::language

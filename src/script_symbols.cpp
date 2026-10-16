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

#include "script_symbols.h"
#include "script_lexer.h"
#include <QRegularExpression>
#include <QSet>

namespace core::scripting {

QStringList ScriptSymbols::variableNames() const {
    QStringList names;
    QSet<QString> seen;
    auto add = [&](const QVector<Variable> &vars) {
        for (const Variable &v : vars) {
            if (!seen.contains(v.name)) {
                seen.insert(v.name);
                names.append(v.name);
            }
        }
    };
    add(definitions);
    add(references);
    return names;
}

ScriptSymbols scanSymbols(const QString &source) {
    static const QRegularExpression interpolated(QStringLiteral("\\$[A-Za-z_]\\w*"));
    ScriptSymbols symbols;
    ScriptLexer lexer;
    const QStringList lines = source.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")));
    for (int i = 0; i < lines.size(); ++i) {
        const int lineNo = i + 1;
        const TokenLine tokens = lexer.tokenizeLine(lines[i], lineNo);
        if (tokens.isEmpty())
            continue;
        const TokenType first = tokens[0].type;

        if (first == TokenType::LABEL && tokens.size() > 1) {
            symbols.labels.append({tokens[1].value, lineNo});
            continue;
        }

        if (first == TokenType::DEF && tokens.size() > 1) {
            ScriptSymbols::Function fn;
            fn.name = tokens[1].value;
            fn.line = lineNo;
            for (int t = 2; t < tokens.size(); ++t) {
                if (tokens[t].type == TokenType::VARIABLE) {
                    fn.params.append(tokens[t].value);
                    symbols.definitions.append(
                        {tokens[t].value, lineNo, tokens[t].column, tokens[t].length});
                }
            }
            symbols.functions.append(fn);
            continue;
        }

        const bool assigns = first == TokenType::SET || first == TokenType::EXTRACT;
        for (int t = 0; t < tokens.size(); ++t) {
            const ScriptToken &tok = tokens[t];
            if (tok.type == TokenType::VARIABLE) {
                ScriptSymbols::Variable var{tok.value, lineNo, tok.column, tok.length};
                if (assigns && t == 1)
                    symbols.definitions.append(var);
                else
                    symbols.references.append(var);
            } else if (tok.type == TokenType::STRING_LITERAL) {
                // $NAME inside a quoted string, or in an unquoted LOG message,
                // is interpolated at run time. Columns are approximate when
                // the string contains escapes.
                const bool quoted = lines[i].mid(tok.column - 1, 1) == QLatin1String("\"");
                if (!quoted && !(first == TokenType::LOG && t == 1))
                    continue;
                auto it = interpolated.globalMatch(tok.value);
                while (it.hasNext()) {
                    const auto m = it.next();
                    symbols.references.append({m.captured(), lineNo,
                                               tok.column + (quoted ? 1 : 0)
                                                   + static_cast<int>(m.capturedStart()),
                                               static_cast<int>(m.capturedLength())});
                }
            }
        }
    }
    return symbols;
}

} // namespace core::scripting

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

#include "script_linter.h"
#include "script_language.h"
#include "script_lexer.h"
#include "script_parser.h"
#include "script_symbols.h"
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace core::scripting {

namespace {

// Span of the code on a line (without indentation and trailing comment), as
// a 1-based column and a length. Empty lines get column 1, length 0.
QPair<int, int> codeSpan(const QString &text) {
    int end = language::commentStart(text);
    if (end < 0)
        end = text.length();
    int start = 0;
    while (start < end && text[start].isSpace())
        ++start;
    while (end > start && text[end - 1].isSpace())
        --end;
    return {start + 1, end - start};
}

} // namespace

QVector<Diagnostic> lint(const QString &source) {
    QVector<Diagnostic> diagnostics;
    const QStringList lines = source.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")));
    auto lineDiagnostic = [&](int line, Diagnostic::Severity severity, const QString &message) {
        Diagnostic d;
        d.line = line;
        d.severity = severity;
        d.message = message;
        if (line >= 1 && line <= lines.size()) {
            const auto span = codeSpan(lines[line - 1]);
            d.column = span.first;
            d.length = span.second;
        } else {
            d.column = 1;
        }
        return d;
    };

    // Parser errors (block matching, unknown targets, argument counts, ...)
    ScriptParser parser;
    const ParseResult parsed = parser.parse(source);
    QSet<QString> seen;
    for (const ParseError &err : parsed.errors) {
        const QString key = QString::number(err.line) + '\n' + err.message;
        if (seen.contains(key))
            continue;
        seen.insert(key);
        diagnostics.append(lineDiagnostic(err.line, Diagnostic::Severity::Error, err.message));
    }

    // Unreachable code: the statement right after ABORT, GOTO or RETURN,
    // unless it ends or splits the block or is a jump target
    ScriptLexer lexer;
    QString terminator;
    for (int i = 0; i < lines.size(); ++i) {
        const TokenLine tokens = lexer.tokenizeLine(lines[i], i + 1);
        if (tokens.isEmpty())
            continue;
        const TokenType first = tokens[0].type;
        if (!terminator.isEmpty()) {
            switch (first) {
            case TokenType::LABEL: case TokenType::ELSE: case TokenType::ENDIF:
            case TokenType::ENDWHILE: case TokenType::ENDREPEAT: case TokenType::ENDDEF:
                break;
            default:
                diagnostics.append(lineDiagnostic(i + 1, Diagnostic::Severity::Warning,
                    QString("Unreachable code after %1").arg(terminator)));
            }
        }
        terminator.clear();
        if (first == TokenType::ABORT || first == TokenType::GOTO || first == TokenType::RETURN)
            terminator = tokens[0].value;
    }

    // Variables that are assigned but never read
    const ScriptSymbols symbols = scanSymbols(source);
    QSet<QString> referenced;
    for (const auto &ref : symbols.references)
        referenced.insert(ref.name);
    QSet<QString> reported;
    for (const auto &def : symbols.definitions) {
        if (referenced.contains(def.name) || reported.contains(def.name))
            continue;
        reported.insert(def.name);
        Diagnostic d;
        d.line = def.line;
        d.column = def.column;
        d.length = def.length;
        d.severity = Diagnostic::Severity::Warning;
        d.message = QString("Variable '%1' is assigned but never used").arg(def.name);
        diagnostics.append(d);
    }

    std::stable_sort(diagnostics.begin(), diagnostics.end(),
                     [](const Diagnostic &a, const Diagnostic &b) {
                         return a.line != b.line ? a.line < b.line : a.column < b.column;
                     });
    return diagnostics;
}

} // namespace core::scripting

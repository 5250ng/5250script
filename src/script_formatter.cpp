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

#include "script_formatter.h"
#include "script_language.h"
#include <QRegularExpression>
#include <QStringList>

namespace core::scripting {

QString formatScript(const QString &source) {
    static const QString indentUnit = QStringLiteral("    ");
    static const QStringList openers = {"IF", "WHILE", "REPEAT", "DEF"};
    static const QStringList closers = {"ENDIF", "ENDWHILE", "ENDREPEAT", "ENDDEF"};

    QStringList lines = source.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")));
    const bool trailingNewline = lines.size() > 1 && lines.last().isEmpty();
    if (trailingNewline)
        lines.removeLast();

    int level = 0;
    QStringList out;
    out.reserve(lines.size());
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            out.append(QString());
            continue;
        }
        int commentAt = language::commentStart(trimmed);
        const QString code = (commentAt >= 0 ? trimmed.left(commentAt) : trimmed).trimmed();
        const QString keyword = code.section(QRegularExpression(QStringLiteral("[\\s(]")), 0, 0).toUpper();

        const bool isElse = keyword == QLatin1String("ELSE");
        if (closers.contains(keyword) || isElse)
            level = qMax(0, level - 1);
        out.append(indentUnit.repeated(level) + trimmed);
        if (openers.contains(keyword) || isElse)
            ++level;
    }
    QString result = out.join('\n');
    if (trailingNewline)
        result += '\n';
    return result;
}

} // namespace core::scripting

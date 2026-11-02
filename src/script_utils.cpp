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

#include "script_utils.h"
#include "script_lexer.h"
#include "script_token.h"
#include <QSet>
#include <algorithm>

namespace core::scripting {

QStringList extractSessionVariables(const QString &scriptSource) {
    if (scriptSource.isEmpty())
        return {};

    ScriptLexer lexer;
    QVector<TokenLine> lines = lexer.tokenize(scriptSource);

    QSet<QString> seen;
    for (const TokenLine &line : lines) {
        for (const ScriptToken &tok : line) {
            if (tok.type == TokenType::VARIABLE &&
                tok.value.startsWith("$SESSION_")) {
                seen.insert(tok.value);
            }
        }
    }

    QStringList result(seen.begin(), seen.end());
    std::sort(result.begin(), result.end());
    return result;
}

} // namespace core::scripting

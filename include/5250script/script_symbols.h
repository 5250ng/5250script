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

#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace core::scripting {

// Symbols declared and used in a script, found by lexing each line. Works
// on scripts that do not parse, so editors can offer them while typing.
struct ScriptSymbols {
    struct Label {
        QString name;
        int line = 0;    // 1-based
    };
    struct Function {
        QString name;
        QStringList params;  // "$p1", "$p2", ...
        int line = 0;
    };
    struct Variable {
        QString name;    // "$NAME"
        int line = 0;
        int column = 0;  // 1-based column of the '$'
        int length = 0;
    };

    QVector<Label> labels;
    QVector<Function> functions;
    // Variables assigned by SET or EXTRACT, and DEF parameters
    QVector<Variable> definitions;
    // Every other $NAME, including INPUT targets, INC/DEC/ADD targets and
    // $NAME interpolated inside string literals
    QVector<Variable> references;

    // Unique variable names from definitions and references, in order of
    // first appearance
    QStringList variableNames() const;
};

ScriptSymbols scanSymbols(const QString &source);

} // namespace core::scripting

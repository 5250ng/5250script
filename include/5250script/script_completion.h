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
#include <QVector>

namespace core::scripting {

struct CompletionItem {
    enum class Kind { Keyword, Key, Variable, Label, Function };

    QString label;       // what the list shows
    QString insertText;  // replaces the typed prefix
    QString detail;      // short description, e.g. "Label (line 12)"
    Kind kind = Kind::Keyword;
};

struct CompletionResult {
    int replaceStart = 0;  // 0-based offset in the line where the prefix starts
    QString prefix;        // text between replaceStart and the cursor
    QVector<CompletionItem> items;
};

// Context-aware completion for the cursor at (line, column) in source.
// line is 1-based; column is the 0-based offset of the cursor in that line.
// Items are already filtered by the typed prefix (case-insensitive). When
// the prefix is all lower-case, keywords are inserted in lower case too
// (the lexer accepts all-upper or all-lower keywords).
CompletionResult complete(const QString &source, int line, int column);

} // namespace core::scripting

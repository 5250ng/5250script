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

#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <cstdint>

namespace core::scripting {

struct ScriptMetadata {
    QMap<QString, QString> values;
    QString name() const        { return values.value("script.name"); }
    QString author() const      { return values.value("script.author"); }
    QString description() const { return values.value("script.description"); }
    QString version() const     { return values.value("script.version"); }
    QString menuPath() const    { return values.value("menu.path"); }
};

class ScriptCompiler {
  public:
    static QString aidByteToKeyword(uint8_t aidByte);
    /// Parses the leading "# @key = \"value\"" comment lines. Inside the
    /// quotes, \" stands for a quote and \\ for a backslash.
    static ScriptMetadata extractMetadata(const QString &scriptText);
    /// Returns value as a quoted metadata value that extractMetadata()
    /// reads back unchanged.
    static QString quoteMetadataValue(const QString &value);
};

} // namespace core::scripting

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

#include "script_compiler.h"

namespace core::scripting {

QString ScriptCompiler::aidByteToKeyword(uint8_t aidByte) {
    switch (aidByte) {
    case 0xF1: return "ENTER";
    case 0x31: return "F1";
    case 0x32: return "F2";
    case 0x33: return "F3";
    case 0x34: return "F4";
    case 0x35: return "F5";
    case 0x36: return "F6";
    case 0x37: return "F7";
    case 0x38: return "F8";
    case 0x39: return "F9";
    case 0x3A: return "F10";
    case 0x3B: return "F11";
    case 0x3C: return "F12";
    case 0xB1: return "F13";
    case 0xB2: return "F14";
    case 0xB3: return "F15";
    case 0xB4: return "F16";
    case 0xB5: return "F17";
    case 0xB6: return "F18";
    case 0xB7: return "F19";
    case 0xB8: return "F20";
    case 0xB9: return "F21";
    case 0xBA: return "F22";
    case 0xBB: return "F23";
    case 0xBC: return "F24";
    case 0xF5: return "PAGEUP";
    case 0xF4: return "PAGEDOWN";
    case 0x70: return "ATTN";
    case 0x71: return "SYSREQ";
    case 0xF3: return "HELP";
    case 0xBD: return "CLEAR";
    case 0xF6: return "PRINT";
    default:   return QString("# Unknown AID: 0x%1").arg(aidByte, 2, 16, QChar('0'));
    }
}

ScriptMetadata ScriptCompiler::extractMetadata(const QString &scriptText) {
    ScriptMetadata meta;
    static const QRegularExpression re(R"DELIM(^#\s*@([\w.]+)\s*=\s*"([^"]*)")DELIM");
    const QStringList lines = scriptText.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty())
            continue;
        if (!trimmed.startsWith('#'))
            break;
        auto match = re.match(trimmed);
        if (match.hasMatch())
            meta.values.insert(match.captured(1), match.captured(2));
    }
    return meta;
}

} // namespace core::scripting

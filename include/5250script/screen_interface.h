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

namespace core::scripting {

// Abstract interface for screen access used by ScriptExecutor.
// Host applications implement this to bridge the executor to their
// concrete screen buffer / terminal widget.
class ScreenInterface {
  public:
    virtual ~ScreenInterface() = default;

    enum class KeyboardState { Unlocked, Locked, ErrorLocked, SystemRequest };

    // Screen dimensions
    virtual int rows() const = 0;
    virtual int cols() const = 0;

    // Cursor position (0-based)
    virtual int cursorRow() const = 0;
    virtual int cursorCol() const = 0;

    // Read decoded text from the screen at (row, col) for length characters.
    // Row and col are 0-based. Returns already-decoded Unicode text.
    virtual QString readText(int row, int col, int length) const = 0;

    // Read the decoded text content of the input field at (row, col).
    // Row and col are 0-based. Returns empty string if no field found.
    virtual QString readFieldText(int row, int col) const = 0;

    // Current keyboard state
    virtual KeyboardState keyboardState() const = 0;

    // Whether the message-waiting indicator is active
    virtual bool messageWaiting() const = 0;
};

} // namespace core::scripting

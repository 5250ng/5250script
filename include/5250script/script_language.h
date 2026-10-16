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

#include "script_token.h"
#include <QString>
#include <QStringList>
#include <QVector>

// Language tables and token classification for editors (syntax colouring,
// completion). Everything here is derived from ScriptLexer's keyword table
// so editors cannot drift from what the parser accepts.
namespace core::scripting::language {

enum class TokenCategory {
    Command,      // TYPE, PRESS, MOVE, EXPECT, SET, LOG, ...
    Control,      // IF, WHILE, REPEAT, DEF, CALL, LABEL, GOTO, ON, THEN, ...
    AidKey,       // ENTER, F1-F24, PAGEUP, ...
    LocalKey,     // TAB, BACKTAB, FIELDEXIT, ...
    CharKey,      // KEY_A, KEY_SPACE, ...
    SubKeyword,   // TEXT, AT, CURSOR, KEYBOARD, NOT, ISSET, DELAY, ...
    Operator,     // == != < > <= >= AND OR
    Punctuation,  // ( , )
    Number,
    String,       // quoted string, or the unquoted message of LOG
    Variable,     // $NAME
    Function,     // function name after DEF/CALL or before '('
    Label,        // label name after LABEL, GOTO or ON ... GOTO
    Identifier,   // any other bare word
    Unknown,      // text the lexer could not tokenize
};

// Category of a token type on its own (STRING_LITERAL maps to String).
TokenCategory categoryOf(TokenType type);

// Category of each token on a lexed line, using the line text and the
// surrounding tokens to tell bare identifiers, labels and function names
// apart from quoted strings. The result has one entry per token.
QVector<TokenCategory> classifyLine(const TokenLine &tokens, const QString &lineText);

// Keyword spellings in a category, in token declaration order. Aliases are
// collapsed to their shortest spelling (ESC, not ESCAPE).
QStringList keywords(TokenCategory category);

// Keywords that can start a statement (commands and control keywords
// except THEN).
QStringList statementKeywords();

QStringList aidKeys();
QStringList localKeys();
QStringList charKeys();

// Variables the executor sets itself (see ScriptExecutor).
QStringList builtinVariables();

// Index of the '#' starting a comment on the line, or -1. A '#' inside a
// double-quoted string does not start a comment.
int commentStart(const QString &lineText);

// True for "# @key = value" metadata comment lines.
bool isMetadataComment(const QString &lineText);

} // namespace core::scripting::language

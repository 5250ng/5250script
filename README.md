![](./.github/banner.png)

<p align="center">
  A C++ library for scripting and automating IBM TN5250 terminal emulator interactions.
  <br>
  <img alt="GitHub release (latest by date)" src="https://img.shields.io/github/v/release/5250ng/5250script">
  <a href="https://twitter.com/intent/follow?screen_name=5250ng" title="Follow"><img src="https://img.shields.io/twitter/follow/5250ng?label=5250ng&style=social"></a>
  <br>
</p>

## Features

 - [x] Full scripting language with lexer, parser, AST, and executor pipeline.
 - [x] Screen interaction commands:
    + [x] `TYPE` text input and `PRESS` key simulation (AID keys and local keys).
    + [x] `EXPECT` conditions to wait for screen content, cursor position, or keyboard state.
    + [x] `EXTRACT` data from screen text, fields, cursor position, and rows.
    + [x] `MOVE CURSOR` with absolute coordinates, field index, or directional movement.
 - [x] Variable system with `SET`, `INC`, `DEC`, `ADD`, and string interpolation.
 - [x] Built-in variables: `$TITLE`, `$CURSOR_ROW`, `$CURSOR_COL`, `$KEYBOARD_STATE`, `$MESSAGE_WAITING`, `$EXPECT_RESULT`.
 - [x] Control flow:
    + [x] `IF`/`ELSE`/`ENDIF` conditionals with `==`, `!=`, `<`, `>`, `<=`, `>=`, `CONTAINS`, `ISSET`.
    + [x] Compound conditions with `AND`, `OR`, `NOT`, and parenthesized grouping.
    + [x] `WHILE`/`ENDWHILE` and `REPEAT`/`ENDREPEAT` loops.
    + [x] `LABEL`/`GOTO` for jump-based flow control.
 - [x] User-defined functions with `DEF`/`ENDDEF`, `CALL`, `RETURN`, and parameter scoping.
 - [x] Error handling with `ON TIMEOUT GOTO` and `ON ERROR GOTO`.
 - [x] Timing control with `WAIT`, `GLOBAL DELAY`, `GLOBAL JITTER`, and `GLOBAL EXPECT_TIMEOUT`.
 - [x] Utility commands: `LOG`, `PAUSE`, `INPUT`, `ABORT`.
 - [x] Script metadata via comments (`# @script.name`, `# @script.author`, `# @menu.path`).
 - [x] Abstract `ScreenInterface` for integration with any Qt-based host application.
 - [x] Comprehensive test suite with 160+ test cases.

## Usage

### Building

5250script uses CMake and requires C++20 and Qt6:

```
cmake -B build -DBUILD_5250SCRIPT_TESTS=ON
cmake --build build
ctest --test-dir build
```

### Integration

```cpp
#include <5250script/script_parser.h>
#include <5250script/script_executor.h>

// 1. Parse the script
ScriptParser parser;
ParseResult result = parser.parse(scriptText);

if (result.hasErrors()) {
    for (const auto &err : result.errors) {
        qDebug() << "Line" << err.line << ":" << err.message;
    }
    return;
}

// 2. Create executor with screen implementation
ScriptExecutor executor;
executor.setScreen(screenImpl);

// 3. Set initial variables
QHash<QString, QString> vars;
vars["USERNAME"] = "QSECOFR";
vars["PASSWORD"] = "PASSWORD";
executor.setInitialVariables(vars);

// 4. Connect signals
connect(&executor, &ScriptExecutor::injectAIDKey,
        this, &MainWindow::handleAIDKey);
connect(&executor, &ScriptExecutor::executionFinished,
        this, &MainWindow::onScriptFinished);

// 5. Execute
executor.execute(result);
```

### Script Language

```
# @script.name = "Login Script"
# @script.author = "Admin"

# Set credentials
SET $user "QSECOFR"
SET $pass "PASSWORD"

# Wait for login screen
EXPECT TEXT "User" AT ROW 6
MOVE CURSOR AT INPUTFIELD 1
TYPE $user
PRESS TAB
TYPE $pass
PRESS ENTER

# Verify login succeeded
EXPECT TEXT "Main Menu"
LOG "Login successful"
```

## Example

 + Automating a login with retry logic:

    ```
    DEF login($user, $pass)
        EXPECT TEXT "User" AT ROW 6
        MOVE CURSOR AT INPUTFIELD 1
        TYPE $user
        PRESS TAB
        TYPE $pass
        PRESS ENTER
    ENDDEF

    ON TIMEOUT GOTO retry
    GLOBAL EXPECT_TIMEOUT 5000

    SET $attempts 0

    LABEL retry
    INC $attempts
    IF $attempts > 3
        ABORT "Login failed after 3 attempts"
    ENDIF

    CALL login("QSECOFR", "PASSWORD")
    EXPECT TEXT "Main Menu"
    LOG "Logged in after $attempts attempt(s)"
    ```

 + Extracting screen data with conditions:

    ```
    EXPECT KEYBOARD UNLOCKED
    EXTRACT $title FROM 1 1 LENGTH 40

    IF $title CONTAINS "Error"
        LOG "Error screen detected: $title"
        ABORT "Unexpected error screen"
    ENDIF

    SET $row 1
    WHILE $row <= 24
        EXTRACT $line LINE $row
        IF $line CONTAINS "MSGW"
            LOG "Message waiting on row $row"
        ENDIF
        INC $row
    ENDWHILE
    ```

## Contributing

Pull requests are welcome. Feel free to open an issue if you want to add other features.

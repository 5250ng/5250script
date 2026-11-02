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

#include <5250script/screen_interface.h>
#include <5250script/script_executor.h>
#include <5250script/script_parser.h>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace core::scripting;

// Minimal ScreenInterface stub for executor tests.
class FakeScreen : public ScreenInterface {
  public:
    int rows() const override { return m_rows; }
    int cols() const override { return m_cols; }
    int cursorRow() const override { return 0; }
    int cursorCol() const override { return 0; }
    QString readText(int, int, int length) const override { return QString(length, ' '); }
    QString readFieldText(int, int) const override { return {}; }
    KeyboardState keyboardState() const override { return m_state; }
    bool messageWaiting() const override { return false; }

    void setKeyboardState(KeyboardState s) { m_state = s; }

  private:
    int m_rows = 24;
    int m_cols = 80;
    KeyboardState m_state = KeyboardState::Unlocked;
};

class TestScriptExecutor : public QObject {
    Q_OBJECT

  private slots:
    void onTimeoutFromInsideFunction();
};

// Regression test for #3: ON TIMEOUT must fire even when the EXPECT that
// times out was entered from within a CALL-ed function. Previously the
// handler was suppressed because gotoLabel() refused to run with a non-empty
// call stack, terminating the script with "GOTO is not allowed inside
// functions" instead of dispatching to the user-registered handler.
void TestScriptExecutor::onTimeoutFromInsideFunction() {
    FakeScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);

    const QString source =
        "GLOBAL EXPECT_TIMEOUT 50\n"
        "ON TIMEOUT GOTO handler\n"
        "CALL f()\n"
        "LOG \"should-not-run\"\n"
        "LABEL handler\n"
        "LOG \"caught\"\n"
        "\n"
        "DEF f()\n"
        "    EXPECT TEXT \"never-present\"\n"
        "ENDDEF\n";

    ScriptParser parser;
    auto result = parser.parse(source);
    QVERIFY(!result.hasErrors());

    QSignalSpy logSpy(&executor, &ScriptExecutor::logMessage);
    QSignalSpy errorSpy(&executor, &ScriptExecutor::executionError);
    QSignalSpy finishedSpy(&executor, &ScriptExecutor::executionFinished);

    executor.execute(result);

    QVERIFY(finishedSpy.wait(5000));

    // The handler must have run — LOG "caught" should appear.
    bool caughtLogged = false;
    bool shouldNotRunLogged = false;
    for (const auto &args : logSpy) {
        const QString msg = args.at(0).toString();
        if (msg == "caught") caughtLogged = true;
        if (msg == "should-not-run") shouldNotRunLogged = true;
    }
    QVERIFY(caughtLogged);
    QVERIFY(!shouldNotRunLogged);

    // No "GOTO is not allowed inside functions" error should be emitted.
    for (const auto &args : errorSpy) {
        const QString msg = args.at(1).toString();
        QVERIFY2(!msg.contains("GOTO is not allowed inside functions"),
                 qPrintable(QString("unexpected error: %1").arg(msg)));
    }
}

QTEST_MAIN(TestScriptExecutor)
#include "test_script_executor.moc"

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

#include <5250script/script_executor.h>
#include <5250script/script_parser.h>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace core::scripting;

namespace {

// Screen whose keyboard state the test drives
class FakeScreen : public ScreenInterface {
  public:
    int rows() const override { return 24; }
    int cols() const override { return 80; }
    int cursorRow() const override { return 0; }
    int cursorCol() const override { return 0; }
    QString readText(int, int, int length) const override { return QString(length, ' '); }
    QString readFieldText(int, int) const override { return QString(); }
    KeyboardState keyboardState() const override { return state; }
    bool messageWaiting() const override { return false; }

    KeyboardState state = KeyboardState::Unlocked;
};

// Emulates the screen widget around an auto-enter field of `fieldLength`
// characters: the key that fills the field sends the screen and locks the
// keyboard, keys reaching the locked keyboard are dropped, and the "host"
// unlocks it 30 ms later, or error-locks it when `errorAfterEnter` is set.
// FIELDEXIT (Ctrl+End) also enters the field.
struct AutoEnterHost {
    QString accepted;   // keys the widget took, with '|' for each auto enter
    QString dropped;    // keys discarded by the locked keyboard
    bool errorAfterEnter = false;
    bool stopWhileLocked = false;

    void run(const QString &source, int fieldLength) {
        ScriptParser parser;
        ParseResult parsed = parser.parse(source);
        QVERIFY(!parsed.hasErrors());
        FakeScreen screen;
        ScriptExecutor executor;
        executor.setScreen(&screen);
        int inField = 0;
        auto enter = [&]() {
            accepted += '|';
            inField = 0;
            screen.state = ScreenInterface::KeyboardState::Locked;
            if (stopWhileLocked) {
                QTimer::singleShot(10, &executor, [&]() { executor.stop(); });
            }
            QTimer::singleShot(30, &executor, [&]() {
                // An error-locked keyboard takes keys again for the ON ERROR
                // handler (in 5250ng, Reset); unlock it once the state is seen
                screen.state = errorAfterEnter ? ScreenInterface::KeyboardState::ErrorLocked
                                               : ScreenInterface::KeyboardState::Unlocked;
                executor.notifyTerminalStateChanged();
                errorAfterEnter = false;
                screen.state = ScreenInterface::KeyboardState::Unlocked;
            });
        };
        QObject::connect(&executor, &ScriptExecutor::injectKeyPress, &executor,
                         [&](int key, Qt::KeyboardModifiers mods, const QString &text) {
            const bool fieldExit = key == Qt::Key_End && mods == Qt::ControlModifier;
            if (screen.state != ScreenInterface::KeyboardState::Unlocked) {
                dropped += fieldExit ? QStringLiteral("<FE>") : text;
                return;
            }
            if (fieldExit) {
                enter();
                return;
            }
            accepted += text;
            if (++inField == fieldLength)
                enter();
        });
        QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
        executor.execute(parsed);
        QVERIFY(finished.wait(5000));
        // Let any late timer or stray resume run before the result is read
        QTest::qWait(100);
    }
};

} // namespace

// Regression tests for 5250ng/5250ng#323: input that followed an auto enter
// was injected while the keyboard was locked and dropped.
class TestScriptExecutorAutoEnter : public QObject {
    Q_OBJECT

  private slots:
    void testTypeWaitsForUnlockInsideStatement();
    void testNextStatementWaitsAfterTypeFillsField();
    void testCharWaitsAfterFillingField();
    void testFieldExitWaitsForUnlock();
    void testAlreadyLockedKeyboardDoesNotBlock();
    void testErrorAfterAutoEnterDropsRestOfType();
    void testStopWhileWaitingTypesNothingMore();
};

void TestScriptExecutorAutoEnter::testTypeWaitsForUnlockInsideStatement() {
    AutoEnterHost host;
    host.run("TYPE \"abcdefg\"\n", 3);
    QCOMPARE(host.dropped, QString());
    QCOMPARE(host.accepted, QStringLiteral("abc|def|g"));
}

void TestScriptExecutorAutoEnter::testNextStatementWaitsAfterTypeFillsField() {
    AutoEnterHost host;
    host.run("TYPE \"abc\"\nTYPE \"xy\"\n", 3);
    QCOMPARE(host.dropped, QString());
    QCOMPARE(host.accepted, QStringLiteral("abc|xy"));
}

void TestScriptExecutorAutoEnter::testCharWaitsAfterFillingField() {
    AutoEnterHost host;
    host.run("PRESS KEY_A\nPRESS KEY_B\nPRESS KEY_C\nTYPE \"z\"\n", 3);
    QCOMPARE(host.dropped, QString());
    QCOMPARE(host.accepted.toLower(), QStringLiteral("abc|z"));
}

void TestScriptExecutorAutoEnter::testFieldExitWaitsForUnlock() {
    AutoEnterHost host;
    host.run("TYPE \"a\"\nPRESS FIELDEXIT\nTYPE \"b\"\n", 3);
    QCOMPARE(host.dropped, QString());
    QCOMPARE(host.accepted, QStringLiteral("a|b"));
}

void TestScriptExecutorAutoEnter::testAlreadyLockedKeyboardDoesNotBlock() {
    // A keyboard that was locked before the key (not by it) does not make
    // the executor wait: the script still runs to the end.
    ScriptParser parser;
    ParseResult parsed = parser.parse("TYPE \"ab\"\nTYPE \"c\"\n");
    FakeScreen screen;
    screen.state = ScreenInterface::KeyboardState::Locked;
    ScriptExecutor executor;
    executor.setScreen(&screen);
    QString typed;
    QObject::connect(&executor, &ScriptExecutor::injectKeyPress, &executor,
                     [&](int, Qt::KeyboardModifiers, const QString &text) { typed += text; });
    QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
    executor.execute(parsed);
    QVERIFY(finished.wait(2000));
    QCOMPARE(typed, QStringLiteral("abc"));
}

void TestScriptExecutorAutoEnter::testErrorAfterAutoEnterDropsRestOfType() {
    // The host answers the auto enter with an error: the ON ERROR handler
    // runs, and the rest of the interrupted TYPE is not typed at all.
    AutoEnterHost host;
    host.errorAfterEnter = true;
    host.run("ON ERROR GOTO handler\n"
             "TYPE \"abcSECRET\"\n"
             "TYPE \"never\"\n"
             "LABEL handler\n"
             "TYPE \"z\"\n", 3);
    QCOMPARE(host.dropped, QString());
    QCOMPARE(host.accepted, QStringLiteral("abc|z"));
}

void TestScriptExecutorAutoEnter::testStopWhileWaitingTypesNothingMore() {
    AutoEnterHost host;
    host.stopWhileLocked = true;
    host.run("TYPE \"abcdef\"\nTYPE \"gh\"\n", 3);
    QCOMPARE(host.dropped, QString());
    QCOMPARE(host.accepted, QStringLiteral("abc|"));
}

QTEST_MAIN(TestScriptExecutorAutoEnter)
#include "test_script_executor_auto_enter.moc"

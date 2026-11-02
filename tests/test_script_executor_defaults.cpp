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
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QtTest/QtTest>

using namespace core::scripting;

namespace {

// Blank 24x80 screen with the keyboard unlocked
class FakeScreen : public ScreenInterface {
  public:
    int rows() const override { return 24; }
    int cols() const override { return 80; }
    int cursorRow() const override { return 0; }
    int cursorCol() const override { return 0; }
    QString readText(int, int, int length) const override { return QString(length, ' '); }
    QString readFieldText(int, int) const override { return QString(); }
    KeyboardState keyboardState() const override { return KeyboardState::Unlocked; }
    bool messageWaiting() const override { return false; }
};

} // namespace

class TestScriptExecutorDefaults : public QObject {
    Q_OBJECT

  private slots:
    void testBuiltInDefaults();
    void testSetDefaultsNormalises();
    void testDefaultExpectTimeoutApplies();
    void testScriptGlobalOverridesDefault();
    void testDefaultActionDelayApplies();
    void testDefaultsPersistAcrossRuns();
};

void TestScriptExecutorDefaults::testBuiltInDefaults() {
    ScriptExecutor executor;
    const ExecutionDefaults d = executor.defaults();
    QCOMPARE(d.expectTimeoutMs, 30000);
    QCOMPARE(d.actionDelayMs, 0);
    QCOMPARE(d.jitterMinMs, 0);
    QCOMPARE(d.jitterMaxMs, 0);
}

void TestScriptExecutorDefaults::testSetDefaultsNormalises() {
    ScriptExecutor executor;
    executor.setDefaults({-5, -1, 300, 100});
    const ExecutionDefaults d = executor.defaults();
    QCOMPARE(d.expectTimeoutMs, 0);
    QCOMPARE(d.actionDelayMs, 0);
    QCOMPARE(d.jitterMinMs, 100);  // swapped
    QCOMPARE(d.jitterMaxMs, 300);
}

void TestScriptExecutorDefaults::testDefaultExpectTimeoutApplies() {
    FakeScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);
    executor.setDefaults({200, 0, 0, 0});
    QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
    QStringList logs;
    connect(&executor, &ScriptExecutor::logMessage, this, [&](const QString &m) { logs << m; });

    ScriptParser parser;
    QElapsedTimer timer;
    timer.start();
    executor.execute(parser.parse("EXPECT TEXT \"never on screen\"\nLOG \"$EXPECT_RESULT\"\n"));
    // With the built-in 30 s timeout this would not finish in time
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    QVERIFY(timer.elapsed() >= 150);
    QVERIFY(logs.join(' ').contains("TIMEOUT"));
}

void TestScriptExecutorDefaults::testScriptGlobalOverridesDefault() {
    FakeScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);
    executor.setDefaults({60000, 0, 0, 0});
    QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
    ScriptParser parser;
    executor.execute(parser.parse("GLOBAL EXPECT_TIMEOUT 100\nEXPECT TEXT \"never\"\n"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
}

void TestScriptExecutorDefaults::testDefaultActionDelayApplies() {
    FakeScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);
    executor.setDefaults({30000, 150, 0, 0});
    QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
    ScriptParser parser;
    QElapsedTimer timer;
    timer.start();
    executor.execute(parser.parse("LOG a\nLOG b\nLOG c\n"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    QVERIFY2(timer.elapsed() >= 300, qPrintable(QString::number(timer.elapsed())));
}

void TestScriptExecutorDefaults::testDefaultsPersistAcrossRuns() {
    FakeScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);
    executor.setDefaults({100, 0, 0, 0});
    QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
    ScriptParser parser;
    // The first run changes its own timeout; the second starts from the default again
    executor.execute(parser.parse("GLOBAL EXPECT_TIMEOUT 60000\nLOG x\n"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
    executor.execute(parser.parse("EXPECT TEXT \"never\"\n"));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
    QCOMPARE(executor.defaults().expectTimeoutMs, 100);
}

QTEST_MAIN(TestScriptExecutorDefaults)
#include "test_script_executor_defaults.moc"

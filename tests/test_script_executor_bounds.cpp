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

// Fake screen whose readFieldText records the coordinates it receives, so the
// test can assert that out-of-range coordinates are filtered out by
// ScriptExecutor before they reach the host screen implementation.
class RecordingScreen : public ScreenInterface {
  public:
    int rows() const override { return 24; }
    int cols() const override { return 80; }
    int cursorRow() const override { return 0; }
    int cursorCol() const override { return 0; }
    QString readText(int, int, int length) const override { return QString(length, ' '); }
    QString readFieldText(int row, int col) const override {
        m_fieldCalls.append(QPoint(row, col));
        return QStringLiteral("field-content");
    }
    KeyboardState keyboardState() const override { return KeyboardState::Unlocked; }
    bool messageWaiting() const override { return false; }

    mutable QVector<QPoint> m_fieldCalls;
};

class TestScriptExecutorBounds : public QObject {
    Q_OBJECT
  private slots:
    void readFieldTextFiltersOutOfRangeCoordinates();
};

// Regression test for #4: EXPECT FIELD AT / EXTRACT FIELD AT with row/col
// outside the screen dimensions must not reach ScreenInterface::readFieldText.
void TestScriptExecutorBounds::readFieldTextFiltersOutOfRangeCoordinates() {
    RecordingScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);

    // All three EXTRACTs are out of bounds after the 1-based → 0-based conversion:
    //   ( 0, 0) -> (-1, -1)      negative row, negative col
    //   (99,99) -> (98, 98)      row beyond rows(), col beyond cols()
    //   (1, 99) -> ( 0, 98)      valid row, col beyond cols()
    // A single in-bounds extract at (1,1) -> (0,0) must still reach the screen.
    const QString source =
        "EXTRACT $A FIELD AT 0 0\n"
        "EXTRACT $B FIELD AT 99 99\n"
        "EXTRACT $C FIELD AT 1 99\n"
        "EXTRACT $D FIELD AT 1 1\n";

    ScriptParser parser;
    auto result = parser.parse(source);
    QVERIFY(!result.hasErrors());

    QSignalSpy finishedSpy(&executor, &ScriptExecutor::executionFinished);
    executor.execute(result);
    QVERIFY(finishedSpy.wait(5000));

    // Only the single in-bounds EXTRACT should have reached the screen.
    QCOMPARE(screen.m_fieldCalls.size(), 1);
    QCOMPARE(screen.m_fieldCalls.first(), QPoint(0, 0));
}

QTEST_MAIN(TestScriptExecutorBounds)
#include "test_script_executor_bounds.moc"

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

// Regression tests for 5250ng/5250ng#404: MOVE CURSOR UP/DOWN/LEFT/RIGHT n
// emitted one step per unit of n with no upper bound, freezing the UI thread.
class TestScriptMoveCursorSteps : public QObject {
    Q_OBJECT

    // Run one script on a 24x80 screen and count the steps it emits
    static int stepsFor(const QString &script, QString *direction = nullptr) {
        FakeScreen screen;
        ScriptExecutor executor;
        executor.setScreen(&screen);
        int steps = 0;
        bool finished = false;
        connect(&executor, &ScriptExecutor::moveCursorStep, &executor, [&](const QString &dir) {
            ++steps;
            if (direction) *direction = dir;
        });
        connect(&executor, &ScriptExecutor::executionFinished, &executor, [&] { finished = true; });
        ScriptParser parser;
        executor.execute(parser.parse(script));
        QElapsedTimer timer;
        timer.start();
        while (!finished && timer.elapsed() < 5000) QCoreApplication::processEvents();
        return finished ? steps : -1;
    }

  private slots:
    void countsInRangeAreKept() {
        QString dir;
        QCOMPARE(stepsFor("MOVE CURSOR UP 3", &dir), 3);
        QCOMPARE(dir, QStringLiteral("UP"));
        QCOMPARE(stepsFor("MOVE CURSOR DOWN 23"), 23);
        QCOMPARE(stepsFor("MOVE CURSOR LEFT 5"), 5);
        // Left and right wrap from row to row, so a move longer than a row still counts
        QCOMPARE(stepsFor("MOVE CURSOR RIGHT 100"), 100);
        QCOMPARE(stepsFor("MOVE CURSOR RIGHT 1919"), 1919);
    }

    void countsPastTheEdgeAreBounded() {
        QCOMPARE(stepsFor("MOVE CURSOR UP 24"), 23);
        QCOMPARE(stepsFor("MOVE CURSOR DOWN 200"), 23);
        QCOMPARE(stepsFor("MOVE CURSOR LEFT 1920"), 1919);
        QCOMPARE(stepsFor("MOVE CURSOR RIGHT 5000"), 1919);
    }

    void hugeCountsFinishQuickly() {
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(stepsFor("MOVE CURSOR UP 2147483647"), 23);
        QCOMPARE(stepsFor("MOVE CURSOR RIGHT 2147483647"), 1919);
        QVERIFY(timer.elapsed() < 5000);
    }
};

QTEST_MAIN(TestScriptMoveCursorSteps)
#include "test_script_move_cursor_steps.moc"

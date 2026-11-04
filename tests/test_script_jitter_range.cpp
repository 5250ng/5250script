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
#include <QRandomGenerator>
#include <QtTest/QtTest>
#include <climits>

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

// Regression tests for 5250ng/5250ng#416: GLOBAL JITTER 0 2147483647 computed
// max - min + 1 in int, which overflowed to INT_MIN before reaching
// QRandomGenerator::bounded().
class TestScriptJitterRange : public QObject {
    Q_OBJECT

  private slots:
    // Each draw stays inside [min, max] for ranges up to the full int range
    void drawStaysInRange_data() {
        QTest::addColumn<int>("minMs");
        QTest::addColumn<int>("maxMs");
        QTest::newRow("full range") << 0 << INT_MAX;
        QTest::newRow("top two") << INT_MAX - 1 << INT_MAX;
        QTest::newRow("single max") << INT_MAX << INT_MAX;
        QTest::newRow("zero") << 0 << 0;
        QTest::newRow("small") << 3 << 7;
        QTest::newRow("swapped") << 7 << 3;
    }
    void drawStaysInRange() {
        QFETCH(int, minMs);
        QFETCH(int, maxMs);
        const int lo = qMin(minMs, maxMs);
        const int hi = qMax(minMs, maxMs);
        QRandomGenerator rng(416);
        bool sawLo = false, sawHi = false;
        for (int i = 0; i < 20000; ++i) {
            const int v = ScriptExecutor::randomJitterMs(minMs, maxMs, &rng);
            QVERIFY2(v >= lo && v <= hi, qPrintable(QString::number(v)));
            sawLo |= v == lo;
            sawHi |= v == hi;
        }
        // Narrow ranges reach both ends, so the upper bound is inclusive
        if (qint64(hi) - lo < 8) {
            QVERIFY(sawLo);
            QVERIFY(sawHi);
        }
    }

    // A wide full-range sample is not stuck in the lower half, as a negative
    // or truncated bound would leave it
    void fullRangeReachesUpperHalf() {
        QRandomGenerator rng(4160);
        bool upper = false;
        for (int i = 0; i < 1000 && !upper; ++i)
            upper = ScriptExecutor::randomJitterMs(0, INT_MAX, &rng) > INT_MAX / 2;
        QVERIFY(upper);
    }

    // The executor accepts the full range and schedules the next step
    // without overflowing; stop() ends the run before the long delay
    void executorAcceptsFullRange() {
        FakeScreen screen;
        ScriptExecutor executor;
        executor.setScreen(&screen);
        ScriptParser parser;
        executor.execute(parser.parse("GLOBAL JITTER 0 2147483647\nLOG \"after\"\n"));
        QCoreApplication::processEvents();
        QVERIFY(executor.isRunning());
        executor.stop();
        QVERIFY(!executor.isRunning());
    }
};

QTEST_GUILESS_MAIN(TestScriptJitterRange)
#include "test_script_jitter_range.moc"

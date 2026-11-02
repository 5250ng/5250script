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

struct InjectedKey {
    int key = 0;
    Qt::KeyboardModifiers mods = Qt::NoModifier;
};

// Run "PRESS <name>" and collect every key event the executor injects.
QList<InjectedKey> runPress(const QString &name) {
    ScriptParser parser;
    ParseResult parsed = parser.parse(QStringLiteral("PRESS ") + name);
    if (!parsed.errors.isEmpty()) return {};

    FakeScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);
    QList<InjectedKey> keys;
    QObject::connect(&executor, &ScriptExecutor::injectKeyPress,
                     [&keys](int key, Qt::KeyboardModifiers mods, const QString &) {
                         keys.append({key, mods});
                     });
    QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
    executor.execute(parsed);
    if (finished.isEmpty()) finished.wait(2000);
    return keys;
}

} // namespace

class TestScriptExecutorLocalKeys : public QObject {
    Q_OBJECT

  private slots:
    void testEveryLocalKeyInjectsOneKey_data();
    void testEveryLocalKeyInjectsOneKey();
    void testEscapeAliasMatchesEsc();
};

void TestScriptExecutorLocalKeys::testEveryLocalKeyInjectsOneKey_data() {
    QTest::addColumn<QString>("name");
    const char *names[] = {"TAB", "BACKTAB", "BACKSPACE", "DELETE", "INSERT", "HOME",
                           "END", "ESC", "ESCAPE", "FIELDPLUS", "FIELDMINUS",
                           "FIELDEXIT", "DUP", "ERASEINPUT", "ERASEFIELD", "ERASEEOF"};
    for (const char *n : names) QTest::newRow(n) << QString::fromLatin1(n);
}

// Every keyword the parser accepts as a local key must reach the host;
// none may be silently dropped by the executor's name match.
void TestScriptExecutorLocalKeys::testEveryLocalKeyInjectsOneKey() {
    QFETCH(QString, name);
    const QList<InjectedKey> keys = runPress(name);
    QCOMPARE(keys.size(), 1);
    QVERIFY(keys.first().key != 0);
}

// ESCAPE is a lexer alias of ESC, so it must inject the same chord.
void TestScriptExecutorLocalKeys::testEscapeAliasMatchesEsc() {
    const QList<InjectedKey> esc = runPress("ESC");
    const QList<InjectedKey> escape = runPress("ESCAPE");
    QCOMPARE(esc.size(), 1);
    QCOMPARE(escape.size(), 1);
    QCOMPARE(escape.first().key, static_cast<int>(Qt::Key_Escape));
    QCOMPARE(escape.first().key, esc.first().key);
    QCOMPARE(escape.first().mods, esc.first().mods);
}

QTEST_MAIN(TestScriptExecutorLocalKeys)
#include "test_script_executor_local_keys.moc"

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

struct Run {
    QStringList logs;
    QString typed;
    bool finished = false;
};

Run runScript(const QString &source, const QHash<QString, QString> &initial) {
    Run run;
    ScriptParser parser;
    ParseResult parsed = parser.parse(source);
    if (parsed.hasErrors())
        return run;
    FakeScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);
    executor.setInitialVariables(initial);
    QObject::connect(&executor, &ScriptExecutor::logMessage,
                     [&](const QString &m) { run.logs << m; });
    QObject::connect(&executor, &ScriptExecutor::injectKeyPress,
                     [&](int, Qt::KeyboardModifiers, const QString &text) { run.typed += text; });
    QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
    executor.execute(parsed);
    if (finished.isEmpty())
        finished.wait(5000);
    run.finished = !finished.isEmpty();
    return run;
}

} // namespace

// $SESSION_* variables come from the session profile. One the user left
// blank is undefined (ISSET is false) but still reads as empty text.
class TestScriptSessionVariables : public QObject {
    Q_OBJECT

  private slots:
    void undefinedSessionVariableIsEmptyText() {
        const Run run = runScript(QStringLiteral(
            "TYPE \"[$SESSION_PASSWORD]\"\n"
            "IF $SESSION_USERNAME == \"\" THEN\n"
            "    LOG \"blank\"\n"
            "ENDIF\n"), {});
        QVERIFY(run.finished);
        QCOMPARE(run.typed, QStringLiteral("[]"));
        QCOMPARE(run.logs, QStringList{QStringLiteral("blank")});
    }

    void undefinedSessionVariableIsNotSet() {
        const Run run = runScript(QStringLiteral(
            "IF ISSET $SESSION_USERNAME THEN\n"
            "    LOG \"set\"\n"
            "ELSE\n"
            "    LOG \"unset\"\n"
            "ENDIF\n"), {});
        QCOMPARE(run.logs, QStringList{QStringLiteral("unset")});
    }

    void definedSessionVariableIsTyped() {
        const Run run = runScript(QStringLiteral("TYPE \"$SESSION_USERNAME\"\n"),
                                  {{QStringLiteral("$SESSION_USERNAME"), QStringLiteral("BOB")}});
        QCOMPARE(run.typed, QStringLiteral("BOB"));
    }

    // Other undefined variables keep reading as 0 (counters without SET).
    void otherUndefinedVariableIsZero() {
        const Run run = runScript(QStringLiteral("TYPE \"$COUNT\"\nINC $N\nTYPE \"$N\"\n"), {});
        QCOMPARE(run.typed, QStringLiteral("01"));
    }
};

QTEST_GUILESS_MAIN(TestScriptSessionVariables)
#include "test_script_session_variables.moc"

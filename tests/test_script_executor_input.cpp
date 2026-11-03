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

// Runs a script, answering every INPUT prompt from `answers` (by variable
// name), and records what the executor shows, logs and types.
struct Run {
    QList<QStringList> labels;
    QList<QList<bool>> masked;
    QStringList logs;
    QStringList errors;
    QString typed;
};

Run runScript(const QString &source, const QHash<QString, QString> &answers) {
    Run run;
    ScriptParser parser;
    ParseResult parsed = parser.parse(source);
    if (parsed.hasErrors()) {
        run.errors << QStringLiteral("parse error");
        return run;
    }
    FakeScreen screen;
    ScriptExecutor executor;
    executor.setScreen(&screen);
    QObject::connect(&executor, &ScriptExecutor::inputRequested, &executor,
                     [&](const QStringList &labels, const QStringList &varNames,
                         const QList<bool> &masked) {
        run.labels << labels;
        run.masked << masked;
        QStringList values;
        for (const QString &name : varNames)
            values << answers.value(name);
        QTimer::singleShot(0, &executor, [&executor, varNames, values]() {
            executor.resumeAfterInput(varNames, values);
        });
    });
    QObject::connect(&executor, &ScriptExecutor::logMessage,
                     [&](const QString &m) { run.logs << m; });
    QObject::connect(&executor, &ScriptExecutor::executionError,
                     [&](int, const QString &m) { run.errors << m; });
    QObject::connect(&executor, &ScriptExecutor::injectKeyPress,
                     [&](int, Qt::KeyboardModifiers, const QString &text) { run.typed += text; });
    QSignalSpy finished(&executor, &ScriptExecutor::executionFinished);
    executor.execute(parsed);
    if (finished.isEmpty())
        finished.wait(5000);
    return run;
}

const QHash<QString, QString> kAnswers = {{"$USER", "BOB"}, {"$PASS", "S3CRET"}};

} // namespace

class TestScriptExecutorInput : public QObject {
    Q_OBJECT

  private slots:
    void testMaskedFlagsAreBatched();
    void testPasswordIsTypedButMaskedInLog();
    void testPasswordMaskedInAbortMessage();
    void testSecretFollowsSet();
    void testSecretFollowsFunctionArgument();
    void testPlainInputIsNotMasked();
};

void TestScriptExecutorInput::testMaskedFlagsAreBatched() {
    Run run = runScript("INPUT \"User:\" $USER\n"
                        "INPUT PASSWORD \"Password:\" $PASS\n", kAnswers);
    QCOMPARE(run.labels.size(), 1);
    QCOMPARE(run.labels[0], (QStringList{"User:", "Password:"}));
    QCOMPARE(run.masked[0], (QList<bool>{false, true}));
}

void TestScriptExecutorInput::testPasswordIsTypedButMaskedInLog() {
    Run run = runScript("INPUT \"User:\" $USER\n"
                        "INPUT PASSWORD \"Password:\" $PASS\n"
                        "TYPE \"$PASS\"\n"
                        "LOG \"user=$USER pass=$PASS\"\n", kAnswers);
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errors.join('\n')));
    QCOMPARE(run.typed, QStringLiteral("S3CRET"));
    QCOMPARE(run.logs, QStringList{"user=BOB pass=********"});
}

void TestScriptExecutorInput::testPasswordMaskedInAbortMessage() {
    Run run = runScript("INPUT PASSWORD \"Password:\" $PASS\n"
                        "ABORT \"Rejected $PASS\"\n", kAnswers);
    QCOMPARE(run.errors, QStringList{"Rejected ********"});
}

// A value built from a secret stays secret; overwriting it clears that.
void TestScriptExecutorInput::testSecretFollowsSet() {
    Run run = runScript("INPUT PASSWORD \"Password:\" $PASS\n"
                        "SET $COPY \"pw:$PASS\"\n"
                        "LOG \"$COPY\"\n"
                        "SET $COPY \"plain\"\n"
                        "LOG \"$COPY\"\n"
                        "SET $PASS \"reset\"\n"
                        "LOG \"$PASS\"\n", kAnswers);
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errors.join('\n')));
    QCOMPARE(run.logs, (QStringList{"********", "plain", "reset"}));
}

void TestScriptExecutorInput::testSecretFollowsFunctionArgument() {
    Run run = runScript("DEF show($value)\n"
                        "    LOG \"in=$value\"\n"
                        "ENDDEF\n"
                        "INPUT PASSWORD \"Password:\" $PASS\n"
                        "SET $value \"outer\"\n"
                        "show($PASS)\n"
                        "LOG \"out=$value\"\n", kAnswers);
    QVERIFY2(run.errors.isEmpty(), qPrintable(run.errors.join('\n')));
    // Inside the call the parameter holds the password; after it, the
    // caller's own $value is restored together with its (plain) status.
    QCOMPARE(run.logs, (QStringList{"in=********", "out=outer"}));
}

void TestScriptExecutorInput::testPlainInputIsNotMasked() {
    Run run = runScript("INPUT \"User:\" $USER\n"
                        "LOG \"$USER\"\n", kAnswers);
    QCOMPARE(run.masked[0], QList<bool>{false});
    QCOMPARE(run.logs, QStringList{"BOB"});
}

QTEST_MAIN(TestScriptExecutorInput)
#include "test_script_executor_input.moc"

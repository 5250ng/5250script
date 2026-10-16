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

#include <5250script/script_completion.h>
#include <QtTest/QtTest>

using namespace core::scripting;

namespace {

// Complete at the '|' marker in the script and return the item labels
QStringList labelsAt(const QString &marked, CompletionResult *out = nullptr) {
    const int marker = marked.indexOf('|');
    QString source = marked;
    source.remove(marker, 1);
    const QString before = source.left(marker);
    const int line = before.count('\n') + 1;
    const int column = marker - (before.lastIndexOf('\n') + 1);
    const CompletionResult result = complete(source, line, column);
    if (out)
        *out = result;
    QStringList labels;
    for (const auto &item : result.items)
        labels << item.label;
    return labels;
}

} // namespace

class TestScriptCompletion : public QObject {
    Q_OBJECT

  private slots:
    void testEmptyLineOffersStatementsAndAidKeys();
    void testPrefixFiltersAndReplacesWord();
    void testLowerCasePrefixInsertsLowerCase();
    void testPressOffersKeys();
    void testMoveChain();
    void testExpectChain();
    void testExtractGlobalOn();
    void testLabelsAndFunctions();
    void testConditions();
    void testVariables();
    void testNoCompletionInCommentsOrStrings();
};

void TestScriptCompletion::testEmptyLineOffersStatementsAndAidKeys() {
    const QStringList labels = labelsAt("|");
    QVERIFY(labels.contains("TYPE"));
    QVERIFY(labels.contains("IF"));
    QVERIFY(labels.contains("ENTER"));
    QVERIFY(labels.contains("F3"));
    QVERIFY(!labels.contains("THEN"));
    QVERIFY(!labels.contains("TAB"));  // local keys need PRESS
}

void TestScriptCompletion::testPrefixFiltersAndReplacesWord() {
    CompletionResult result;
    const QStringList labels = labelsAt("    EX|", &result);
    QCOMPARE(labels, (QStringList{"EXPECT", "EXTRACT"}));
    QCOMPARE(result.replaceStart, 4);
    QCOMPARE(result.prefix, QString("EX"));
    // A fully typed keyword closes the list
    QVERIFY(!labelsAt("WAIT|").contains("WAIT"));
}

void TestScriptCompletion::testLowerCasePrefixInsertsLowerCase() {
    CompletionResult result;
    labelsAt("pre|", &result);
    QCOMPARE(result.items.size(), 1);
    QCOMPARE(result.items[0].label, QString("PRESS"));
    QCOMPARE(result.items[0].insertText, QString("press"));
}

void TestScriptCompletion::testPressOffersKeys() {
    const QStringList labels = labelsAt("PRESS |");
    QVERIFY(labels.contains("ENTER"));
    QVERIFY(labels.contains("FIELDEXIT"));
    QVERIFY(labels.contains("KEY_SPACE"));
    QCOMPARE(labelsAt("PRESS TA|"), QStringList{"TAB"});
    QVERIFY(labelsAt("PRESS ENTER |").isEmpty());
}

void TestScriptCompletion::testMoveChain() {
    QCOMPARE(labelsAt("MOVE |"), QStringList{"CURSOR"});
    QCOMPARE(labelsAt("MOVE CURSOR |"), (QStringList{"AT", "UP", "DOWN", "LEFT", "RIGHT"}));
    QCOMPARE(labelsAt("MOVE CURSOR AT |"), (QStringList{"NEXT", "PREVIOUS", "INPUTFIELD"}));
    QCOMPARE(labelsAt("MOVE CURSOR AT NEXT |"), QStringList{"INPUTFIELD"});
}

void TestScriptCompletion::testExpectChain() {
    QCOMPARE(labelsAt("EXPECT |"),
             (QStringList{"TEXT", "CURSOR", "KEYBOARD", "FIELD", "MESSAGEWAITING", "NOT"}));
    QVERIFY(!labelsAt("EXPECT NOT |").contains("NOT"));
    QCOMPARE(labelsAt("EXPECT KEYBOARD |"), (QStringList{"UNLOCKED", "ERRORLOCKED"}));
    QCOMPARE(labelsAt("EXPECT NOT KEYBOARD |"), (QStringList{"UNLOCKED", "ERRORLOCKED"}));
    QCOMPARE(labelsAt("EXPECT TEXT \"Sign On\" |"), QStringList{"AT"});
    QCOMPARE(labelsAt("EXPECT TEXT \"Sign On\" AT |"), QStringList{"ROW"});
    QCOMPARE(labelsAt("EXPECT CURSOR |"), QStringList{"AT"});
    QCOMPARE(labelsAt("EXPECT FIELD AT 6 53 |"), QStringList{"CONTAINS"});
}

void TestScriptCompletion::testExtractGlobalOn() {
    QCOMPARE(labelsAt("EXTRACT $v |"), (QStringList{"FROM", "FIELD", "CURSOR", "LINE"}));
    QCOMPARE(labelsAt("EXTRACT $v CURSOR |"), (QStringList{"ROW", "COL"}));
    QCOMPARE(labelsAt("EXTRACT $v FROM 1 2 |"), QStringList{"LENGTH"});
    QCOMPARE(labelsAt("GLOBAL |"), (QStringList{"EXPECT_TIMEOUT", "DELAY", "JITTER"}));
    QCOMPARE(labelsAt("ON |"), (QStringList{"TIMEOUT", "ERROR"}));
    QCOMPARE(labelsAt("ON TIMEOUT |"), QStringList{"GOTO"});
}

void TestScriptCompletion::testLabelsAndFunctions() {
    const QString script = QStringLiteral(
        "LABEL start\n"
        "DEF login($user, $pass)\n"
        "ENDDEF\n"
        "LABEL retry\n");
    QCOMPARE(labelsAt(script + "GOTO |"), (QStringList{"start", "retry"}));
    QCOMPARE(labelsAt(script + "ON ERROR GOTO re|"), QStringList{"retry"});

    CompletionResult result;
    QCOMPARE(labelsAt(script + "CALL |", &result), QStringList{"login"});
    QCOMPARE(result.items[0].insertText, QString("login($user, $pass)"));
    QCOMPARE(result.items[0].kind, CompletionItem::Kind::Function);
    // Bare calls at the start of a line
    QVERIFY(labelsAt(script + "lo|").contains("login"));
}

void TestScriptCompletion::testConditions() {
    QCOMPARE(labelsAt("IF |"), (QStringList{"NOT", "ISSET"}));
    QCOMPARE(labelsAt("WHILE $n < 5 |"), (QStringList{"AND", "OR"}));
    QCOMPARE(labelsAt("IF $a == \"x\" |"), (QStringList{"AND", "OR", "THEN"}));
    QCOMPARE(labelsAt("IF ISSET($a) |"), (QStringList{"AND", "OR", "THEN"}));
    QCOMPARE(labelsAt("IF $a == 1 AND |"), (QStringList{"NOT", "ISSET"}));
    QCOMPARE(labelsAt("IF NOT |"), QStringList{"ISSET"});
}

void TestScriptCompletion::testVariables() {
    const QString script = QStringLiteral(
        "SET $name \"x\"\n"
        "EXTRACT $title FROM 1 1 LENGTH 10\n"
        "TYPE \"$SESSION_USER\"\n");
    CompletionResult result;
    const QStringList labels = labelsAt(script + "LOG $|", &result);
    QVERIFY(labels.contains("$name"));
    QVERIFY(labels.contains("$title"));
    QVERIFY(labels.contains("$SESSION_USER"));
    QVERIFY(labels.contains("$CURSOR_ROW"));
    QCOMPARE(result.replaceStart, 4);
    QCOMPARE(labelsAt(script + "TYPE \"hello $ti|"), QStringList{"$title"});  // inside a string
}

void TestScriptCompletion::testNoCompletionInCommentsOrStrings() {
    QVERIFY(labelsAt("# PRE|").isEmpty());
    QVERIFY(labelsAt("WAIT 10 # PRE|").isEmpty());
    QVERIFY(labelsAt("TYPE \"PRE|").isEmpty());
}

QTEST_MAIN(TestScriptCompletion)
#include "test_script_completion.moc"

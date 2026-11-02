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

#include <5250script/script_linter.h>
#include <QtTest/QtTest>

using namespace core::scripting;
using Severity = Diagnostic::Severity;

class TestScriptLinter : public QObject {
    Q_OBJECT

  private slots:
    void testCleanScript();
    void testParserErrorsBecomeLineDiagnostics();
    void testUnreachableCode();
    void testUnusedVariables();
    void testSortedByPosition();
};

void TestScriptLinter::testCleanScript() {
    const QString script = QStringLiteral(
        "# @script.name = \"Login\"\n"
        "SET $user \"QSECOFR\"\n"
        "DEF login($u)\n"
        "    TYPE \"$u\"\n"
        "    PRESS ENTER\n"
        "ENDDEF\n"
        "CALL login($user)\n"
        "LOG done\n");
    const auto diags = lint(script);
    QVERIFY2(diags.isEmpty(), diags.isEmpty() ? "" : qPrintable(diags.first().message));
}

void TestScriptLinter::testParserErrorsBecomeLineDiagnostics() {
    const auto diags = lint(QStringLiteral("WAIT 10\n    GOTO nowhere   # oops\n"));
    QCOMPARE(diags.size(), 1);
    QCOMPARE(diags[0].severity, Severity::Error);
    QCOMPARE(diags[0].line, 2);
    QCOMPARE(diags[0].column, 5);   // after the indentation
    QCOMPARE(diags[0].length, 12);  // "GOTO nowhere", without the comment
    QVERIFY(diags[0].message.contains("nowhere"));

    const auto unclosed = lint(QStringLiteral("IF $a == 1\nWAIT 1\n"));
    QCOMPARE(unclosed.size(), 1);
    QVERIFY(unclosed[0].message.contains("Unclosed"));
}

void TestScriptLinter::testUnreachableCode() {
    const auto diags = lint(QStringLiteral(
        "LABEL top\n"
        "IF $x == 1\n"
        "    ABORT \"stop\"\n"
        "    WAIT 10\n"      // unreachable
        "    WAIT 20\n"      // only the first line is reported
        "ELSE\n"
        "    GOTO top\n"
        "ENDIF\n"            // block end: fine
        "GOTO top\n"
        "# comment\n"
        "\n"
        "LABEL after\n"));   // jump target: fine
    QStringList warnings;
    for (const auto &d : diags) {
        if (d.message.startsWith("Unreachable"))
            warnings << QString("%1:%2").arg(d.line).arg(d.message);
    }
    QCOMPARE(warnings, QStringList{"4:Unreachable code after ABORT"});
}

void TestScriptLinter::testUnusedVariables() {
    const auto diags = lint(QStringLiteral(
        "SET $used 1\n"
        "SET  $unused 2\n"
        "SET $unused 3\n"       // reported once, at the first assignment
        "EXTRACT $title FROM 1 1 LENGTH 5\n"
        "INPUT \"Name:\" $name\n"  // INPUT is not counted as an assignment
        "LOG title is $title\n"   // unquoted LOG messages interpolate
        "IF $used == 1\n"
        "ENDIF\n"));
    QCOMPARE(diags.size(), 1);
    QCOMPARE(diags[0].severity, Severity::Warning);
    QCOMPARE(diags[0].line, 2);
    QCOMPARE(diags[0].column, 6);
    QCOMPARE(diags[0].length, 7);
    QVERIFY(diags[0].message.contains("$unused"));
}

void TestScriptLinter::testSortedByPosition() {
    const auto diags = lint(QStringLiteral("SET $a 1\nGOTO missing\nENDIF\n"));
    QVERIFY(diags.size() >= 3);
    for (int i = 1; i < diags.size(); ++i)
        QVERIFY(diags[i - 1].line <= diags[i].line);
}

QTEST_MAIN(TestScriptLinter)
#include "test_script_linter.moc"

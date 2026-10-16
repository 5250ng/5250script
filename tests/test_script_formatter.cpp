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

#include <5250script/script_formatter.h>
#include <QtTest/QtTest>

using namespace core::scripting;

class TestScriptFormatter : public QObject {
    Q_OBJECT

  private slots:
    void testIndentsBlocks();
    void testKeepsTrailingNewlineAndNormalisesEndings();
    void testUnbalancedClosersDoNotGoNegative();
    void testIdempotent();
};

void TestScriptFormatter::testIndentsBlocks() {
    const QString in = QStringLiteral(
        "DEF login($u)\n"
        "IF ISSET($u)   \n"
        "TYPE \"$u\"\n"
        "# inside\n"
        "else\n"
        "ABORT\n"
        "endif\n"
        "ENDDEF\n"
        "REPEAT 3\n"
        "  WHILE $i < 2\n"
        "INC $i\n"
        "ENDWHILE\n"
        "ENDREPEAT");
    const QString expected = QStringLiteral(
        "DEF login($u)\n"
        "    IF ISSET($u)\n"
        "        TYPE \"$u\"\n"
        "        # inside\n"
        "    else\n"
        "        ABORT\n"
        "    endif\n"
        "ENDDEF\n"
        "REPEAT 3\n"
        "    WHILE $i < 2\n"
        "        INC $i\n"
        "    ENDWHILE\n"
        "ENDREPEAT");
    QCOMPARE(formatScript(in), expected);
}

void TestScriptFormatter::testKeepsTrailingNewlineAndNormalisesEndings() {
    QCOMPARE(formatScript(QStringLiteral("WAIT 1  \r\n\r\nPRESS ENTER\r\n")),
             QStringLiteral("WAIT 1\n\nPRESS ENTER\n"));
    QCOMPARE(formatScript(QStringLiteral("WAIT 1")), QStringLiteral("WAIT 1"));
}

void TestScriptFormatter::testUnbalancedClosersDoNotGoNegative() {
    QCOMPARE(formatScript(QStringLiteral("ENDIF\nENDIF\nWAIT 1")),
             QStringLiteral("ENDIF\nENDIF\nWAIT 1"));
}

void TestScriptFormatter::testIdempotent() {
    const QString once = formatScript(QStringLiteral("IF $a == 1\nWAIT 1\nENDIF\n"));
    QCOMPARE(formatScript(once), once);
}

QTEST_MAIN(TestScriptFormatter)
#include "test_script_formatter.moc"

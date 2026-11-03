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

// Metadata values written by quoteMetadataValue() must read back unchanged.

#include "5250script/script_compiler.h"
#include <QTest>

using core::scripting::ScriptCompiler;

class TestScriptMetadata : public QObject {
    Q_OBJECT

  private slots:
    void roundTrip_data() {
        QTest::addColumn<QString>("value");
        QTest::newRow("plain") << QString("Login");
        QTest::newRow("quotes") << QString("my \"quoted\" macro");
        QTest::newRow("backslash") << QString("C:\\scripts\\");
        QTest::newRow("backslash-quote") << QString("a\\\"b");
        QTest::newRow("empty") << QString();
    }

    void roundTrip() {
        QFETCH(QString, value);
        const QString script = "# @script.name = " + ScriptCompiler::quoteMetadataValue(value) + "\n"
                               "# @menu.path = " + ScriptCompiler::quoteMetadataValue(value) + "\n"
                               "TYPE \"x\"\n";
        const auto meta = ScriptCompiler::extractMetadata(script);
        QCOMPARE(meta.name(), value);
        QCOMPARE(meta.menuPath(), value);
    }

    void escapedQuoteIsNotTheEnd() {
        const auto meta = ScriptCompiler::extractMetadata(
            "# @script.name = \"my \\\"quoted\\\" macro\"\n");
        QCOMPARE(meta.name(), QString("my \"quoted\" macro"));
    }

    void longEscapedValue() {
        const QString value = QString("\"").repeated(100000);
        const auto meta = ScriptCompiler::extractMetadata(
            "# @script.name = " + ScriptCompiler::quoteMetadataValue(value) + "\n");
        QCOMPARE(meta.name(), value);
    }

    // Values written before escaping existed keep reading as before.
    void unescapedValuesStillRead() {
        const auto meta = ScriptCompiler::extractMetadata(
            "# @script.name = \"Login\"\n"
            "# @menu.path = \"Tools\\Admin\"\n"
            "# @script.description = \"ends in C:\\\"\n");
        QCOMPARE(meta.name(), QString("Login"));
        QCOMPARE(meta.menuPath(), QString("Tools\\Admin"));
        QCOMPARE(meta.description(), QString("ends in C:\\"));
    }
};

QTEST_GUILESS_MAIN(TestScriptMetadata)
#include "test_script_metadata.moc"

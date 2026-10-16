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

#include <5250script/script_language.h>
#include <5250script/script_lexer.h>
#include <QtTest/QtTest>

using namespace core::scripting;
using language::TokenCategory;

class TestScriptLanguage : public QObject {
    Q_OBJECT

  private slots:
    void testEveryKeywordHasACategory();
    void testKeywordListsComeFromLexer();
    void testStatementKeywords();
    void testClassifyLine();
    void testCommentStart();
    void testMetadataComment();
};

void TestScriptLanguage::testEveryKeywordHasACategory() {
    ScriptLexer lexer;
    const auto &table = lexer.keywords();
    for (auto it = table.constBegin(); it != table.constEnd(); ++it)
        QVERIFY2(language::categoryOf(it.value()) != TokenCategory::Unknown, qPrintable(it.key()));
}

void TestScriptLanguage::testKeywordListsComeFromLexer() {
    const QStringList aid = language::aidKeys();
    QCOMPARE(aid.first(), QString("ENTER"));
    QVERIFY(aid.contains("F24"));
    QVERIFY(aid.contains("PAGEDOWN"));
    QCOMPARE(aid.size(), 32);

    const QStringList local = language::localKeys();
    QVERIFY(local.contains("ESC"));
    QVERIFY(!local.contains("ESCAPE"));  // alias collapsed to the shortest spelling
    QVERIFY(local.contains("DELETE"));
    QCOMPARE(local.size(), 15);

    QCOMPARE(language::charKeys(), ScriptLexer::keyCharNames());

    // Every listed keyword lexes back to a token of the same category
    ScriptLexer lexer;
    for (TokenCategory cat : {TokenCategory::Command, TokenCategory::Control, TokenCategory::AidKey,
                              TokenCategory::LocalKey, TokenCategory::SubKeyword,
                              TokenCategory::Operator}) {
        for (const QString &kw : language::keywords(cat)) {
            const TokenLine tl = lexer.tokenizeLine("X " + kw, 1);
            QVERIFY2(tl.size() == 2 && language::categoryOf(tl[1].type) == cat, qPrintable(kw));
        }
    }
}

void TestScriptLanguage::testStatementKeywords() {
    const QStringList s = language::statementKeywords();
    for (const char *kw : {"TYPE", "PRESS", "MOVE", "EXPECT", "EXTRACT", "WAIT", "SET", "INC",
                           "DEC", "ADD", "ABORT", "LOG", "PAUSE", "GLOBAL", "INPUT", "IF", "ELSE",
                           "ENDIF", "WHILE", "ENDWHILE", "REPEAT", "ENDREPEAT", "DEF", "ENDDEF",
                           "CALL", "RETURN", "LABEL", "GOTO", "ON"})
        QVERIFY2(s.contains(kw), kw);
    QVERIFY(!s.contains("THEN"));
    QVERIFY(!s.contains("ENTER"));
}

void TestScriptLanguage::testClassifyLine() {
    ScriptLexer lexer;
    auto classify = [&](const QString &line) {
        return language::classifyLine(lexer.tokenizeLine(line, 1), line);
    };
    using C = TokenCategory;
    QCOMPARE(classify("CALL login($u, \"x\")"),
             (QVector<C>{C::Control, C::Function, C::Punctuation, C::Variable, C::Punctuation,
                         C::String, C::Punctuation}));
    QCOMPARE(classify("login($u)"),
             (QVector<C>{C::Function, C::Punctuation, C::Variable, C::Punctuation}));
    QCOMPARE(classify("GOTO done"), (QVector<C>{C::Control, C::Label}));
    QCOMPARE(classify("LABEL done"), (QVector<C>{C::Control, C::Label}));
    QCOMPARE(classify("LOG unquoted text"), (QVector<C>{C::Command, C::String}));
    QCOMPARE(classify("PRESS KEY_A"), (QVector<C>{C::Command, C::CharKey}));
    QCOMPARE(classify("IF $a >= 3 AND NOT ISSET $b THEN"),
             (QVector<C>{C::Control, C::Variable, C::Operator, C::Number, C::Operator,
                         C::SubKeyword, C::SubKeyword, C::Variable, C::Control}));
    QCOMPARE(classify("ON TIMEOUT GOTO retry"),
             (QVector<C>{C::Control, C::SubKeyword, C::Control, C::Label}));
}

void TestScriptLanguage::testCommentStart() {
    QCOMPARE(language::commentStart("# full line"), 0);
    QCOMPARE(language::commentStart("PRESS ENTER # go"), 12);
    QCOMPARE(language::commentStart("TYPE \"a # not a comment\""), -1);
    QCOMPARE(language::commentStart("TYPE \"a \\\" # still string\" # yes"), 27);
    QCOMPARE(language::commentStart("WAIT 100"), -1);
}

void TestScriptLanguage::testMetadataComment() {
    QVERIFY(language::isMetadataComment("# @script.name = \"Login\""));
    QVERIFY(language::isMetadataComment("  #@menu.path=\"A/B\""));
    QVERIFY(!language::isMetadataComment("# plain comment"));
    QVERIFY(!language::isMetadataComment("LOG @x = 1"));
}

QTEST_MAIN(TestScriptLanguage)
#include "test_script_language.moc"

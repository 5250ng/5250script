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

#include "script_completion.h"
#include "script_language.h"
#include "script_symbols.h"
#include <QRegularExpression>

namespace core::scripting {

namespace {

using Kind = CompletionItem::Kind;

void addAll(QVector<CompletionItem> &items, const QStringList &labels, Kind kind,
            const QString &detail = QString()) {
    for (const QString &label : labels)
        items.append({label, label, detail, kind});
}

void addKeys(QVector<CompletionItem> &items) {
    addAll(items, language::aidKeys(), Kind::Key, QStringLiteral("AID key"));
    addAll(items, language::localKeys(), Kind::Key, QStringLiteral("Local key"));
    addAll(items, language::charKeys(), Kind::Key, QStringLiteral("Character key"));
}

void addLabels(QVector<CompletionItem> &items, const ScriptSymbols &symbols) {
    for (const auto &label : symbols.labels)
        items.append({label.name, label.name, QString("Label (line %1)").arg(label.line), Kind::Label});
}

void addFunctions(QVector<CompletionItem> &items, const ScriptSymbols &symbols) {
    for (const auto &fn : symbols.functions) {
        items.append({fn.name, QString("%1(%2)").arg(fn.name, fn.params.join(", ")),
                      QString("Function (%1 params)").arg(fn.params.size()), Kind::Function});
    }
}

// Whether position `end` of the line is inside a double-quoted string
bool insideString(const QString &line, int end) {
    bool inString = false;
    for (int i = 0; i < end && i < line.length(); ++i) {
        const QChar c = line[i];
        if (inString && c == '\\') {
            ++i;
        } else if (c == '"') {
            inString = !inString;
        }
    }
    return inString;
}

// Whitespace-separated words before the prefix, keeping quoted strings whole
QStringList contextWords(const QString &text) {
    static const QRegularExpression re(QStringLiteral("\"(?:[^\"\\\\]|\\\\.)*\"?|\\S+"));
    QStringList words;
    auto it = re.globalMatch(text);
    while (it.hasNext())
        words.append(it.next().captured());
    return words;
}

bool isValueWord(const QString &w) {
    static const QRegularExpression number(QStringLiteral("^-?\\d+$"));
    return w.startsWith('"') || w.startsWith('$') || number.match(w).hasMatch()
           || w.endsWith(')');
}

QVector<CompletionItem> candidates(const QStringList &words, const ScriptSymbols &symbols) {
    QVector<CompletionItem> items;
    const int n = words.size();
    if (n == 0) {
        addAll(items, language::statementKeywords(), Kind::Keyword);
        addAll(items, language::aidKeys(), Kind::Key, QStringLiteral("AID key"));
        addFunctions(items, symbols);  // bare call: name(args)
        return items;
    }

    QStringList up;
    for (const QString &w : words)
        up.append(w.toUpper());
    const QString &cmd = up[0];
    auto kw = [&](std::initializer_list<const char *> list) {
        for (const char *k : list)
            items.append({QString::fromLatin1(k), QString::fromLatin1(k), QString(), Kind::Keyword});
    };

    if (cmd == "PRESS") {
        if (n == 1) addKeys(items);
    } else if (cmd == "MOVE") {
        if (n == 1) kw({"CURSOR"});
        else if (n == 2 && up[1] == "CURSOR") kw({"AT", "UP", "DOWN", "LEFT", "RIGHT"});
        else if (n == 3 && up[2] == "AT") kw({"NEXT", "PREVIOUS", "INPUTFIELD"});
        else if (n == 4 && up[2] == "AT" && (up[3] == "NEXT" || up[3] == "PREVIOUS")) kw({"INPUTFIELD"});
    } else if (cmd == "EXPECT") {
        QStringList sub = up.mid(1);
        const bool hasNot = !sub.isEmpty() && sub[0] == "NOT";
        if (hasNot) sub.removeFirst();
        const int s = sub.size();
        if (s == 0) {
            kw({"TEXT", "CURSOR", "KEYBOARD", "FIELD", "MESSAGEWAITING"});
            if (!hasNot) kw({"NOT"});
        } else if (sub[0] == "KEYBOARD" && s == 1) {
            kw({"UNLOCKED", "ERRORLOCKED"});
        } else if (sub[0] == "TEXT" && s == 2 && sub[1].startsWith('"')) {
            kw({"AT"});
        } else if ((sub[0] == "TEXT" && s == 3 && sub[2] == "AT") || (sub[0] == "CURSOR" && s == 2 && sub[1] == "AT")) {
            kw({"ROW"});
        } else if ((sub[0] == "CURSOR" || sub[0] == "FIELD") && s == 1) {
            kw({"AT"});
        } else if (sub[0] == "FIELD" && s == 4 && sub[1] == "AT") {
            kw({"CONTAINS"});
        }
    } else if (cmd == "EXTRACT") {
        if (n == 2) kw({"FROM", "FIELD", "CURSOR", "LINE"});
        else if (n == 3 && up[2] == "CURSOR") kw({"ROW", "COL"});
        else if (n == 3 && up[2] == "FIELD") kw({"AT"});
        else if (n == 5 && up[2] == "FROM") kw({"LENGTH"});
    } else if (cmd == "GLOBAL") {
        if (n == 1) kw({"EXPECT_TIMEOUT", "DELAY", "JITTER"});
    } else if (cmd == "ON") {
        if (n == 1) kw({"TIMEOUT", "ERROR"});
        else if (n == 2) kw({"GOTO"});
        else if (n == 3 && up[2] == "GOTO") addLabels(items, symbols);
    } else if (cmd == "GOTO") {
        if (n == 1) addLabels(items, symbols);
    } else if (cmd == "CALL") {
        if (n == 1) addFunctions(items, symbols);
    } else if (cmd == "IF" || cmd == "WHILE") {
        const QString &last = up.last();
        if (n == 1 || last == "AND" || last == "OR") {
            kw({"NOT", "ISSET"});
        } else if (last == "NOT") {
            kw({"ISSET"});
        } else if (isValueWord(words.last())) {
            kw({"AND", "OR"});
            if (cmd == "IF") kw({"THEN"});
        }
    }
    return items;
}

} // namespace

CompletionResult complete(const QString &source, int line, int column) {
    CompletionResult result;
    const QStringList lines = source.split(QRegularExpression(QStringLiteral("\\r\\n|\\r|\\n")));
    if (line < 1 || line > lines.size())
        return result;
    const QString &text = lines[line - 1];
    column = qBound(0, column, static_cast<int>(text.length()));

    // Prefix: identifier characters before the cursor, plus a leading '$'
    int start = column;
    while (start > 0 && (text[start - 1].isLetterOrNumber() || text[start - 1] == '_'))
        --start;
    if (start > 0 && text[start - 1] == '$')
        --start;
    result.replaceStart = start;
    result.prefix = text.mid(start, column - start);

    const QString before = text.left(start);
    const int comment = language::commentStart(before);
    if (comment >= 0)
        return result;

    const ScriptSymbols symbols = scanSymbols(source);
    QVector<CompletionItem> items;
    if (result.prefix.startsWith('$')) {
        // Variables are completed anywhere, including inside strings
        QStringList names = symbols.variableNames();
        for (const QString &v : language::builtinVariables()) {
            if (!names.contains(v))
                names.append(v);
        }
        addAll(items, names, Kind::Variable);
        for (auto &item : items) {
            if (language::builtinVariables().contains(item.label))
                item.detail = QStringLiteral("Built-in variable");
        }
    } else if (!insideString(text, start)) {
        items = candidates(contextWords(before), symbols);
    }

    // Filter by prefix; drop exact matches so a completed word closes the list
    const QString prefix = result.prefix;
    const bool lowerCase = !prefix.isEmpty() && prefix == prefix.toLower() && prefix != prefix.toUpper();
    for (const CompletionItem &item : items) {
        if (!item.label.startsWith(prefix, Qt::CaseInsensitive))
            continue;
        if (item.label.compare(prefix, Qt::CaseInsensitive) == 0)
            continue;
        CompletionItem out = item;
        if (lowerCase && (item.kind == Kind::Keyword || item.kind == Kind::Key))
            out.insertText = item.insertText.toLower();
        result.items.append(out);
    }
    return result;
}

} // namespace core::scripting

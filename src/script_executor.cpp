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

#include "script_executor.h"
#include <QRandomGenerator>
#include <algorithm>
#include <utility>

namespace core::scripting {

ScriptExecutor::ScriptExecutor(QObject *parent)
    : QObject(parent) {
    m_expectTimer.setSingleShot(true);
    connect(&m_expectTimer, &QTimer::timeout, this, [this]() {
        endExpect(false);
    });
}

void ScriptExecutor::setScreen(ScreenInterface *screen) {
    m_screen = screen;
}

void ScriptExecutor::setInitialVariables(const QHash<QString, QString> &vars) {
    m_initialVariables = vars;
}

void ScriptExecutor::setDefaults(const ExecutionDefaults &defaults) {
    m_defaults.expectTimeoutMs = std::max(0, defaults.expectTimeoutMs);
    m_defaults.actionDelayMs = std::max(0, defaults.actionDelayMs);
    m_defaults.jitterMinMs = std::max(0, std::min(defaults.jitterMinMs, defaults.jitterMaxMs));
    m_defaults.jitterMaxMs = std::max(0, std::max(defaults.jitterMinMs, defaults.jitterMaxMs));
}

void ScriptExecutor::execute(const ParseResult &parseResult) {
    if (m_running) return;

    m_parseResult = parseResult;
    m_running = true;
    m_variables.clear();
    m_secretVariables.clear();
    m_pendingSecretInputs.clear();
    m_execStack.clear();
    m_onTimeoutLabel.clear();
    m_onErrorLabel.clear();
    m_timeout = m_defaults.expectTimeoutMs;
    m_actionDelay = m_defaults.actionDelayMs;
    m_jitterMin = m_defaults.jitterMinMs;
    m_jitterMax = m_defaults.jitterMaxMs;
    m_waitingForUnlock = false;
    m_pendingText.clear();
    m_expectActive = false;
    m_functions = parseResult.functions;
    m_callStack.clear();

    // Initialize built-in variables
    setVariable("$EXPECT_RESULT", "OK");
    setVariable("$REPEAT_INDEX", "0");
    updateBuiltinVariables();

    // Apply initial variables (e.g. $SESSION_USERNAME, $SESSION_PASSWORD from session config)
    for (auto it = m_initialVariables.constBegin(); it != m_initialVariables.constEnd(); ++it) {
        setVariable(it.key(), it.value());
    }

    // Push root frame
    m_execStack.append({&m_parseResult.root->children, 0, 0, 0});

    emit executionStarted();
    scheduleNextStep();
}

void ScriptExecutor::stop() {
    m_running = false;
    m_expectTimer.stop();
    m_expectActive = false;
    m_waitingForUnlock = false;
    m_pendingText.clear();
    m_execStack.clear();
    m_callStack.clear();
    emit executionFinished();
}

void ScriptExecutor::resumeAfterPause() {
    if (!m_running) return;
    scheduleNextStep();
}

void ScriptExecutor::resumeAfterInput(const QStringList &varNames, const QStringList &values) {
    if (!m_running) return;
    for (int i = 0; i < varNames.size() && i < values.size(); ++i) {
        setVariable(varNames[i], values[i], m_pendingSecretInputs.contains(varNames[i]));
    }
    m_pendingSecretInputs.clear();
    scheduleNextStep();
}

void ScriptExecutor::notifyScreenChanged() {
    if (!m_running) return;
    updateBuiltinVariables();

    if (m_expectActive && m_expectNode) {
        if (checkExpectCondition(m_expectNode)) {
            endExpect(true);
        }
    }
}

void ScriptExecutor::notifyTerminalStateChanged() {
    if (!m_running) return;
    updateBuiltinVariables();

    // Check if we're waiting for keyboard unlock after an AID key
    if (m_waitingForUnlock && m_screen) {
        auto ks = m_screen->keyboardState();
        if (ks == ScreenInterface::KeyboardState::Unlocked) {
            m_waitingForUnlock = false;
            scheduleNextStep(50); // Small delay after unlock
        } else if (ks == ScreenInterface::KeyboardState::ErrorLocked) {
            // Terminal entered error-locked state — trigger ON ERROR handler if set
            m_waitingForUnlock = false;
            // The rest of an interrupted TYPE belongs to the screen that failed
            m_pendingText.clear();
            if (!m_onErrorLabel.isEmpty()) {
                // If the error happened inside a function, exit the function first
                // so that gotoLabel (which only accepts top-level labels) can dispatch.
                unwindToTopLevel();
                gotoLabel(m_onErrorLabel);
                scheduleNextStep();
            } else {
                emit executionError(0, "Terminal entered error-locked state");
                stop();
            }
            return;
        }
    }

    // Check EXPECT conditions that depend on terminal state
    if (m_expectActive && m_expectNode) {
        if (checkExpectCondition(m_expectNode)) {
            endExpect(true);
        }
    }
}

void ScriptExecutor::scheduleNextStep(int delayMs) {
    if (!m_running) return;
    int effectiveDelay = std::max(delayMs, m_actionDelay);
    if (m_jitterMax > 0) {
        int jitter = m_jitterMin + (QRandomGenerator::global()->bounded(m_jitterMax - m_jitterMin + 1));
        effectiveDelay = std::max(effectiveDelay, jitter);
    }
    if (effectiveDelay > 0)
        QTimer::singleShot(effectiveDelay, this, &ScriptExecutor::executeStep);
    else
        QTimer::singleShot(0, this, &ScriptExecutor::executeStep);
}

bool ScriptExecutor::keyboardLocked() const {
    return m_screen && m_screen->keyboardState() == ScreenInterface::KeyboardState::Locked;
}

// A key that fills or leaves an auto-enter field sends the screen to the
// host and locks the keyboard, like an AID key. Returns true, and waits for
// notifyTerminalStateChanged() to resume, when the key just injected locked
// a keyboard that was unlocked before it (`lockedBefore` false).
bool ScriptExecutor::keyboardLockedByInput(bool lockedBefore) {
    if (lockedBefore || !keyboardLocked())
        return false;
    m_waitingForUnlock = true;
    return true;
}

// Types `text` one key at a time, holding the rest back in m_pendingText
// when a key locks the keyboard.
void ScriptExecutor::injectText(const QString &text) {
    for (qsizetype i = 0; i < text.size(); ++i) {
        const bool lockedBefore = keyboardLocked();
        emit injectKeyPress(0, Qt::NoModifier, QString(text.at(i)));
        if (!m_running)
            return;
        if (keyboardLockedByInput(lockedBefore)) {
            m_pendingText = text.mid(i + 1);
            return;
        }
    }
    scheduleNextStep(10);
}

void ScriptExecutor::executeStep() {
    if (!m_running) return;
    if (m_waitingForUnlock || m_expectActive) return;

    // Finish a TYPE statement interrupted by an auto-enter
    if (!m_pendingText.isEmpty()) {
        injectText(std::exchange(m_pendingText, QString()));
        return;
    }

    // Find next node to execute
    while (!m_execStack.isEmpty()) {
        auto &frame = m_execStack.last();
        if (frame.index < frame.nodes->size()) {
            auto node = (*frame.nodes)[frame.index];
            frame.index++;
            executeNode(node);
            return;
        }

        // Frame exhausted
        // Check if this is a REPEAT block
        if (frame.repeatCount > 0) {
            frame.currentRepeat++;
            if (frame.currentRepeat < frame.repeatCount) {
                frame.index = 0;
                setVariable("$REPEAT_INDEX", QString::number(frame.currentRepeat));
                auto node = (*frame.nodes)[frame.index];
                frame.index++;
                executeNode(node);
                return;
            }
        }

        m_execStack.removeLast();

        // Implicit return: if we've popped back to the call frame's depth, return from function
        if (!m_callStack.isEmpty() &&
            m_execStack.size() == m_callStack.last().execStackDepth) {
            returnFromFunction();
        }
    }

    // All done
    m_running = false;
    emit executionFinished();
}

void ScriptExecutor::executeNode(const std::shared_ptr<ASTNode> &node) {
    if (!m_running) return;

    switch (node->type) {
    case NodeType::Label:
        scheduleNextStep();
        break;

    case NodeType::TypeInput:
        injectText(interpolateVariables(node->stringValue));
        break;

    case NodeType::CharInput: {
        QString text = interpolateVariables(node->stringValue);
        const bool lockedBefore = keyboardLocked();
        if (!text.isEmpty())
            emit injectKeyPress(0, Qt::NoModifier, text.left(1));
        if (!keyboardLockedByInput(lockedBefore))
            scheduleNextStep(10);
        break;
    }

    case NodeType::AIDKey: {
        emit injectAIDKey(node->aidByte);
        // After AID key, wait for keyboard unlock
        m_waitingForUnlock = true;
        // Don't schedule next step — notifyTerminalStateChanged will resume
        break;
    }

    case NodeType::LocalKey: {
        // Map local key names to Qt key events. The chords must match the
        // host's default key bindings (5250ng: core::KeyboardMapping), which
        // resolve them back to the local action; tests/unit/
        // test_script_local_keys.cpp there checks every key round-trips.
        int qtKey = 0;
        Qt::KeyboardModifiers mods = Qt::NoModifier;
        const QString &name = node->stringValue;

        if (name == "TAB")            { qtKey = Qt::Key_Tab; }
        else if (name == "BACKTAB")   { qtKey = Qt::Key_Backtab; }
        else if (name == "BACKSPACE") { qtKey = Qt::Key_Backspace; }
        else if (name == "DELETE")    { qtKey = Qt::Key_Delete; }
        else if (name == "INSERT")    { qtKey = Qt::Key_Insert; }
        else if (name == "HOME")      { qtKey = Qt::Key_Home; }
        else if (name == "END")       { qtKey = Qt::Key_End; }
        else if (name == "ESC" || name == "ESCAPE") { qtKey = Qt::Key_Escape; }
        else if (name == "FIELDPLUS") { qtKey = Qt::Key_Plus; mods = Qt::AltModifier; }
        else if (name == "FIELDMINUS"){ qtKey = Qt::Key_Minus; mods = Qt::AltModifier; }
        else if (name == "FIELDEXIT") { qtKey = Qt::Key_End; mods = Qt::ControlModifier; }
        else if (name == "DUP")       { qtKey = Qt::Key_Insert; mods = Qt::ShiftModifier; }
        else if (name == "ERASEINPUT"){ qtKey = Qt::Key_Delete; mods = Qt::AltModifier; }
        else if (name == "ERASEFIELD"){ qtKey = Qt::Key_Backspace; mods = Qt::ControlModifier; }
        else if (name == "ERASEEOF")  { qtKey = Qt::Key_Delete; mods = Qt::ControlModifier; }

        const bool lockedBefore = keyboardLocked();
        if (qtKey != 0)
            emit injectKeyPress(qtKey, mods, QString());
        if (!keyboardLockedByInput(lockedBefore))
            scheduleNextStep(10);
        break;
    }

    case NodeType::MoveCursorAt:
        switch (node->moveCursorMode) {
        case MoveCursorMode::Position:
            // Convert 1-based (script) to 0-based (internal)
            emit moveCursor(node->intValue - 1, node->intValue2 - 1);
            break;
        case MoveCursorMode::FieldIndex:
            emit gotoInputField(node->intValue);
            break;
        case MoveCursorMode::NextField:
            emit gotoInputField(-1);
            break;
        case MoveCursorMode::PreviousField:
            emit gotoInputField(-2);
            break;
        case MoveCursorMode::StepUp:
        case MoveCursorMode::StepDown:
        case MoveCursorMode::StepLeft:
        case MoveCursorMode::StepRight: {
            const MoveCursorMode mode = node->moveCursorMode;
            const bool wraps = mode == MoveCursorMode::StepLeft || mode == MoveCursorMode::StepRight;
            const QString direction = mode == MoveCursorMode::StepUp     ? QStringLiteral("UP")
                                      : mode == MoveCursorMode::StepDown ? QStringLiteral("DOWN")
                                      : mode == MoveCursorMode::StepLeft ? QStringLiteral("LEFT")
                                                                         : QStringLiteral("RIGHT");
            const int steps = boundedCursorSteps(node->intValue, wraps);
            for (int i = 0; i < steps; ++i)
                emit moveCursorStep(direction);
            break;
        }
        }
        scheduleNextStep();
        break;

    case NodeType::Wait:
        scheduleNextStep(node->intValue);
        break;

    case NodeType::GlobalDelay:
        m_actionDelay = node->intValue;
        scheduleNextStep();
        break;

    case NodeType::GlobalJitter:
        m_jitterMin = node->intValue;
        m_jitterMax = node->intValue2;
        scheduleNextStep();
        break;

    case NodeType::GlobalExpectTimeout:
        m_timeout = node->intValue;
        scheduleNextStep();
        break;

    case NodeType::Set: {
        bool secret = false;
        QString value = interpolateVariables(node->stringValue, &secret);
        setVariable(node->varName, value, secret);
        scheduleNextStep();
        break;
    }

    case NodeType::Inc: {
        QString val = resolveVariable(node->varName);
        setVariable(node->varName, QString::number(val.toInt() + 1));
        scheduleNextStep();
        break;
    }

    case NodeType::Dec: {
        QString val = resolveVariable(node->varName);
        setVariable(node->varName, QString::number(val.toInt() - 1));
        scheduleNextStep();
        break;
    }

    case NodeType::Add: {
        QString val = resolveVariable(node->varName);
        setVariable(node->varName, QString::number(val.toInt() + node->intValue));
        scheduleNextStep();
        break;
    }

    case NodeType::If: {
        bool condResult = node->condition ? evaluateConditionNode(*node->condition) : false;
        if (condResult) {
            if (!node->children.isEmpty()) {
                m_execStack.append({&node->children, 0, 0, 0});
            }
        } else {
            if (!node->elseChildren.isEmpty()) {
                m_execStack.append({&node->elseChildren, 0, 0, 0});
            }
        }
        scheduleNextStep();
        break;
    }

    case NodeType::While: {
        bool condResult = node->condition ? evaluateConditionNode(*node->condition) : false;
        if (condResult) {
            if (!node->children.isEmpty()) {
                // Decrement parent frame index so WHILE is re-executed after body completes
                if (!m_execStack.isEmpty() && m_execStack.last().index > 0) {
                    m_execStack.last().index--;
                }
                m_execStack.append({&node->children, 0, 0, 0});
            }
        }
        // If condition is false, just continue (WHILE node consumed)
        scheduleNextStep();
        break;
    }

    case NodeType::Repeat: {
        if (!node->children.isEmpty() && node->intValue > 0) {
            setVariable("$REPEAT_INDEX", "0");
            m_execStack.append({&node->children, 0, node->intValue, 0});
        }
        scheduleNextStep();
        break;
    }

    case NodeType::FunctionCall: {
        if (!m_functions.contains(node->stringValue)) {
            emit executionError(node->line,
                                QString("Function '%1' not defined").arg(node->stringValue));
            stop();
            return;
        }
        if (m_callStack.size() >= 100) {
            emit executionError(node->line, "Maximum recursion depth (100) exceeded");
            stop();
            return;
        }
        auto &func = m_functions[node->stringValue];
        if (node->argValues.size() != func->paramNames.size()) {
            emit executionError(node->line,
                                QString("Function '%1' expects %2 arguments, got %3")
                                    .arg(node->stringValue)
                                    .arg(func->paramNames.size())
                                    .arg(node->argValues.size()));
            stop();
            return;
        }
        // Create call frame: save current param values
        CallFrame frame;
        frame.execStackDepth = m_execStack.size();
        frame.paramNames = func->paramNames;
        for (const QString &param : func->paramNames) {
            if (m_variables.contains(param))
                frame.savedVariables[param] = m_variables[param];
            if (m_secretVariables.contains(param))
                frame.savedSecrets.insert(param);
        }
        // Bind arguments to parameters
        for (int i = 0; i < func->paramNames.size(); ++i) {
            bool secret = false;
            QString argVal = interpolateVariables(node->argValues[i], &secret);
            setVariable(func->paramNames[i], argVal, secret);
        }
        m_callStack.append(frame);
        m_execStack.append({&func->children, 0, 0, 0});
        scheduleNextStep();
        break;
    }

    case NodeType::Return: {
        if (m_callStack.isEmpty()) {
            emit executionError(node->line, "RETURN outside of function");
            stop();
            return;
        }
        // Pop exec frames down to call frame depth
        while (m_execStack.size() > m_callStack.last().execStackDepth) {
            m_execStack.removeLast();
        }
        returnFromFunction();
        scheduleNextStep();
        break;
    }

    case NodeType::FunctionDef:
        // Should never be executed (extracted from root), skip
        scheduleNextStep();
        break;

    case NodeType::Goto:
        gotoLabel(node->stringValue);
        scheduleNextStep();
        break;

    case NodeType::OnTimeout:
        m_onTimeoutLabel = node->stringValue;
        scheduleNextStep();
        break;

    case NodeType::OnError:
        m_onErrorLabel = node->stringValue;
        scheduleNextStep();
        break;

    case NodeType::Abort:
        if (!node->stringValue.isEmpty()) {
            QString msg = interpolateMessage(node->stringValue);
            emit executionError(node->line, msg);
        }
        stop();
        break;

    case NodeType::Log: {
        QString msg = interpolateMessage(node->stringValue);
        emit logMessage(msg);
        scheduleNextStep();
        break;
    }

    case NodeType::Pause:
        emit pauseRequested();
        // Don't schedule next step — resumeAfterPause() will do it
        break;

    case NodeType::Input: {
        // Batch consecutive INPUT nodes into a single dialog
        QStringList labels;
        QStringList varNames;
        QList<bool> masked;
        labels.append(node->stringValue);
        varNames.append(node->varName);
        masked.append(node->masked);

        // Look ahead for more consecutive INPUT nodes in the current frame
        if (!m_execStack.isEmpty()) {
            auto &frame = m_execStack.last();
            while (frame.index < frame.nodes->size()) {
                auto &nextNode = (*frame.nodes)[frame.index];
                if (nextNode->type != NodeType::Input) break;
                labels.append(nextNode->stringValue);
                varNames.append(nextNode->varName);
                masked.append(nextNode->masked);
                frame.index++;
            }
        }

        m_pendingSecretInputs.clear();
        for (int i = 0; i < varNames.size(); ++i) {
            if (masked[i])
                m_pendingSecretInputs.insert(varNames[i]);
        }
        emit inputRequested(labels, varNames, masked);
        // Don't schedule next step — resumeAfterInput() will do it
        break;
    }

    case NodeType::Expect:
        startExpect(node);
        break;

    case NodeType::Extract: {
        updateBuiltinVariables();
        QString value;
        switch (node->extractType) {
        case ExtractType::FromPosition: {
            int len = node->stringValue.toInt();
            value = readScreenText(node->intValue - 1, node->intValue2 - 1, len);
            break;
        }
        case ExtractType::FieldAt:
            value = readFieldText(node->intValue - 1, node->intValue2 - 1);
            break;
        case ExtractType::CursorRow:
            if (m_screen) {
                value = QString::number(m_screen->cursorRow() + 1); // 1-based
            }
            break;
        case ExtractType::CursorCol:
            if (m_screen) {
                value = QString::number(m_screen->cursorCol() + 1); // 1-based
            }
            break;
        case ExtractType::LineAt: {
            int row = node->intValue - 1; // 1-based to 0-based
            if (m_screen && row >= 0 && row < m_screen->rows()) {
                value = readScreenText(row, 0, m_screen->cols());
            }
            break;
        }
        }
        setVariable(node->varName, value);
        scheduleNextStep();
        break;
    }

    default:
        scheduleNextStep();
        break;
    }
}

// --- EXPECT implementation ---

void ScriptExecutor::startExpect(const std::shared_ptr<ASTNode> &node) {
    updateBuiltinVariables();

    // Check if condition is already met
    if (checkExpectCondition(node)) {
        setVariable("$EXPECT_RESULT", "OK");
        scheduleNextStep();
        return;
    }

    // Start waiting
    m_expectActive = true;
    m_expectNode = node;
    m_expectTimer.start(m_timeout);
}

bool ScriptExecutor::checkExpectCondition(const std::shared_ptr<ASTNode> &node) const {
    if (!m_screen) return false;

    bool result = false;

    switch (node->expectType) {
    case ExpectType::TextAnywhere:
        result = screenContainsText(node->stringValue);
        break;
    case ExpectType::TextAtPos:
        result = screenContainsTextAt(node->stringValue, node->intValue - 1, node->intValue2 - 1);
        break;
    case ExpectType::TextAtRow:
        result = screenContainsTextAtRow(node->stringValue, node->intValue - 1);
        break;
    case ExpectType::CursorAtPos:
        result = (m_screen->cursorRow() == node->intValue - 1 &&
                  m_screen->cursorCol() == node->intValue2 - 1);
        break;
    case ExpectType::CursorAtRow:
        result = (m_screen->cursorRow() == node->intValue - 1);
        break;
    case ExpectType::KeyboardUnlocked:
        result = (m_screen->keyboardState() == ScreenInterface::KeyboardState::Unlocked);
        break;
    case ExpectType::KeyboardErrorLocked:
        result = (m_screen->keyboardState() == ScreenInterface::KeyboardState::ErrorLocked);
        break;
    case ExpectType::FieldContains: {
        QString fieldText = readFieldText(node->intValue - 1, node->intValue2 - 1);
        result = fieldText.contains(node->stringValue);
        break;
    }
    case ExpectType::MessageWaiting:
        result = m_screen->messageWaiting();
        break;
    }

    if (node->negated) result = !result;
    return result;
}

void ScriptExecutor::endExpect(bool success) {
    m_expectTimer.stop();
    m_expectActive = false;
    m_expectNode = nullptr;

    if (success) {
        setVariable("$EXPECT_RESULT", "OK");
        scheduleNextStep();
    } else {
        setVariable("$EXPECT_RESULT", "TIMEOUT");
        if (!m_onTimeoutLabel.isEmpty()) {
            // If the timeout happened inside a function, exit the function first
            // so that gotoLabel (which only accepts top-level labels) can dispatch.
            unwindToTopLevel();
            gotoLabel(m_onTimeoutLabel);
            scheduleNextStep();
        } else {
            scheduleNextStep();
        }
    }
}

// --- Condition evaluation ---

bool ScriptExecutor::evaluateCondition(const QString &left, CompareOp op, const QString &right) const {
    // CONTAINS is always a string operation
    if (op == CompareOp::Contains)
        return left.contains(right);

    // IsSet is handled directly in executeNode, should never reach here
    if (op == CompareOp::IsSet)
        return false;

    // Try numeric comparison first
    bool leftIsNum = false, rightIsNum = false;
    int leftNum = left.toInt(&leftIsNum);
    int rightNum = right.toInt(&rightIsNum);

    if (leftIsNum && rightIsNum) {
        switch (op) {
        case CompareOp::Eq: return leftNum == rightNum;
        case CompareOp::Ne: return leftNum != rightNum;
        case CompareOp::Lt: return leftNum < rightNum;
        case CompareOp::Gt: return leftNum > rightNum;
        case CompareOp::Le: return leftNum <= rightNum;
        case CompareOp::Ge: return leftNum >= rightNum;
        case CompareOp::Contains: break; // handled above
        case CompareOp::IsSet: break;    // handled in executeNode
        }
    }

    // String comparison
    int cmp = left.compare(right);
    switch (op) {
    case CompareOp::Eq: return cmp == 0;
    case CompareOp::Ne: return cmp != 0;
    case CompareOp::Lt: return cmp < 0;
    case CompareOp::Gt: return cmp > 0;
    case CompareOp::Le: return cmp <= 0;
    case CompareOp::Ge: return cmp >= 0;
    case CompareOp::Contains: break; // handled above
    case CompareOp::IsSet: break;    // handled in executeNode
    }
    return false;
}

bool ScriptExecutor::evaluateConditionNode(const ConditionNode &node) const {
    bool result;

    if (node.logicalOp != LogicalOp::None) {
        // Compound node
        bool lhs = node.lhs ? evaluateConditionNode(*node.lhs) : false;
        bool rhs = node.rhs ? evaluateConditionNode(*node.rhs) : false;
        result = (node.logicalOp == LogicalOp::And) ? (lhs && rhs) : (lhs || rhs);
    } else {
        // Leaf node
        if (node.op == CompareOp::IsSet) {
            result = m_variables.contains(node.left);
        } else {
            QString left = interpolateVariables(node.left);
            QString right = interpolateVariables(node.right);
            result = evaluateCondition(left, node.op, right);
        }
    }

    if (node.negated) result = !result;
    return result;
}

// --- Variable support ---

QString ScriptExecutor::resolveVariable(const QString &name) const {
    // name includes the $ prefix
    auto it = m_variables.constFind(name);
    if (it != m_variables.constEnd())
        return it.value();
    // An undefined variable reads as 0 so counters work without a SET. A
    // $SESSION_* variable is text from the session profile (user name,
    // password, ...) that the user left blank: it reads as empty text, so
    // TYPE "$SESSION_PASSWORD" types nothing rather than "0".
    return name.startsWith(QLatin1String("$SESSION_")) ? QString() : QStringLiteral("0");
}

void ScriptExecutor::setVariable(const QString &name, const QString &value, bool secret) {
    m_variables[name] = value;
    if (secret)
        m_secretVariables.insert(name);
    else
        m_secretVariables.remove(name);
}

QString ScriptExecutor::interpolateVariables(const QString &text, bool *usedSecret) const {
    return interpolate(text, false, usedSecret);
}

QString ScriptExecutor::interpolateMessage(const QString &text) const {
    return interpolate(text, true, nullptr);
}

QString ScriptExecutor::interpolate(const QString &text, bool maskSecrets, bool *usedSecret) const {
    if (usedSecret) *usedSecret = false;
    QString result;
    int i = 0;
    while (i < text.length()) {
        if (text[i] == '$') {
            if (i + 1 < text.length() && text[i + 1] == '$') {
                // Escaped $
                result += '$';
                i += 2;
            } else {
                // Read variable name
                int start = i;
                i++; // skip $
                while (i < text.length() && (text[i].isLetterOrNumber() || text[i] == '_')) i++;
                if (i - start <= 1) {
                    // Lone $ with no variable name — emit literal $
                    result += '$';
                } else {
                    QString varName = text.mid(start, i - start);
                    const bool secret = m_secretVariables.contains(varName);
                    if (secret && usedSecret) *usedSecret = true;
                    result += (secret && maskSecrets) ? QStringLiteral("********")
                                                      : resolveVariable(varName);
                }
            }
        } else {
            result += text[i];
            i++;
        }
    }
    return result;
}

// --- GOTO ---

void ScriptExecutor::gotoLabel(const QString &label) {
    if (!m_callStack.isEmpty()) {
        emit executionError(0, "GOTO is not allowed inside functions");
        stop();
        return;
    }
    if (!m_parseResult.labels.contains(label)) {
        emit executionError(0, QString("Label '%1' not found").arg(label));
        stop();
        return;
    }

    // Clear execution stack and jump to label in root
    m_execStack.clear();
    int targetIndex = m_parseResult.labels[label] + 1; // +1 to skip the LABEL node itself
    m_execStack.append({&m_parseResult.root->children, targetIndex, 0, 0});
}

void ScriptExecutor::unwindToTopLevel() {
    while (!m_callStack.isEmpty()) {
        // Pop any exec frames opened inside the current function (nested IF/WHILE/REPEAT bodies)
        while (m_execStack.size() > m_callStack.last().execStackDepth) {
            m_execStack.removeLast();
        }
        returnFromFunction();
    }
}

// --- Function return ---

void ScriptExecutor::returnFromFunction() {
    if (m_callStack.isEmpty()) return;

    CallFrame frame = m_callStack.last();
    m_callStack.removeLast();

    // Restore saved parameter variables
    for (const QString &param : frame.paramNames) {
        if (frame.savedVariables.contains(param)) {
            setVariable(param, frame.savedVariables[param], frame.savedSecrets.contains(param));
        } else {
            m_variables.remove(param);
            m_secretVariables.remove(param);
        }
    }
}

// --- Built-in variables ---

void ScriptExecutor::updateBuiltinVariables() {
    if (!m_screen) return;

    m_variables["$SCREEN_ROWS"] = QString::number(m_screen->rows());
    m_variables["$SCREEN_COLS"] = QString::number(m_screen->cols());
    m_variables["$CURSOR_ROW"] = QString::number(m_screen->cursorRow() + 1);
    m_variables["$CURSOR_COL"] = QString::number(m_screen->cursorCol() + 1);

    switch (m_screen->keyboardState()) {
    case ScreenInterface::KeyboardState::Unlocked:
        m_variables["$KEYBOARD_STATE"] = "UNLOCKED";
        break;
    case ScreenInterface::KeyboardState::Locked:
        m_variables["$KEYBOARD_STATE"] = "LOCKED";
        break;
    case ScreenInterface::KeyboardState::ErrorLocked:
        m_variables["$KEYBOARD_STATE"] = "ERRORLOCKED";
        break;
    case ScreenInterface::KeyboardState::SystemRequest:
        m_variables["$KEYBOARD_STATE"] = "SYSTEMREQUEST";
        break;
    }

    m_variables["$MESSAGE_WAITING"] = m_screen->messageWaiting() ? "1" : "0";
}

// --- Screen text helpers ---

// A step past the screen edge does nothing, so a larger count only blocks the
// UI thread (5250ng/5250ng#404). Up and down stop at the top and bottom rows.
// Left and right wrap from row to row but stop at the first and last cells.
int ScriptExecutor::boundedCursorSteps(int count, bool wraps) const {
    // Without a screen, assume the largest 5250 display (27x132)
    const int rows = m_screen ? m_screen->rows() : 27;
    const int cols = m_screen ? m_screen->cols() : 132;
    const qint64 limit = wraps ? qint64(rows) * cols - 1 : qint64(rows) - 1;
    return int(qBound(qint64(0), qint64(count), qMax(qint64(0), limit)));
}

QString ScriptExecutor::readScreenText(int row, int col, int length) const {
    if (!m_screen) return {};
    if (row < 0 || row >= m_screen->rows() || col < 0) return {};
    return m_screen->readText(row, col, length);
}

QString ScriptExecutor::readFieldText(int row, int col) const {
    if (!m_screen) return {};
    if (row < 0 || row >= m_screen->rows() || col < 0 || col >= m_screen->cols())
        return {};
    return m_screen->readFieldText(row, col);
}

bool ScriptExecutor::screenContainsText(const QString &text) const {
    if (!m_screen) return false;
    for (int row = 0; row < m_screen->rows(); ++row) {
        if (m_screen->readText(row, 0, m_screen->cols()).contains(text))
            return true;
    }
    return false;
}

bool ScriptExecutor::screenContainsTextAt(const QString &text, int row, int col) const {
    if (!m_screen) return false;
    if (row < 0 || row >= m_screen->rows() || col < 0) return false;
    QString screenText = m_screen->readText(row, col, text.length());
    return screenText == text;
}

bool ScriptExecutor::screenContainsTextAtRow(const QString &text, int row) const {
    if (!m_screen) return false;
    if (row < 0 || row >= m_screen->rows()) return false;
    return m_screen->readText(row, 0, m_screen->cols()).contains(text);
}

} // namespace core::scripting

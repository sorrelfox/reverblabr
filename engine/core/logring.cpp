// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/logring.h"

#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>

#include <deque>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

struct Ring {
    QMutex mutex;
    std::deque<QString> lines;
    std::size_t capacity = LogRing::kDefaultCapacity;
    bool installed = false;
    QtMessageHandler previous = nullptr;
};

[[nodiscard]] Ring &ring()
{
    // Never destroyed: a message can still arrive while statics are being
    // torn down at exit, and a handler that reached a destroyed mutex then
    // would take the process down with it.
    static Ring *const instance = new Ring;
    return *instance;
}

[[nodiscard]] QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return u"debug"_s;
    case QtInfoMsg:
        return u"info"_s;
    case QtWarningMsg:
        return u"warning"_s;
    case QtCriticalMsg:
        return u"critical"_s;
    case QtFatalMsg:
        return u"fatal"_s;
    }
    return u"debug"_s;
}

void keep(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    Ring &state = ring();
    QtMessageHandler previous = nullptr;
    {
        const QMutexLocker lock(&state.mutex);
        state.lines.push_back(LogRing::formatLine(type, context, message));
        while (state.lines.size() > state.capacity) {
            state.lines.pop_front();
        }
        previous = state.previous;
    }
    // **Outside the lock**: the handler before this one may log in turn, or
    // abort on a fatal message, and neither may happen holding our mutex.
    if (previous != nullptr) {
        previous(type, context, message);
    }
}

} // namespace

void LogRing::install(std::size_t capacity)
{
    Ring &state = ring();
    const QMutexLocker lock(&state.mutex);
    state.capacity = capacity == 0 ? 1 : capacity;
    while (state.lines.size() > state.capacity) {
        state.lines.pop_front();
    }
    if (!state.installed) {
        state.previous = qInstallMessageHandler(&keep);
        state.installed = true;
    }
}

bool LogRing::isInstalled()
{
    Ring &state = ring();
    const QMutexLocker lock(&state.mutex);
    return state.installed;
}

QStringList LogRing::lines()
{
    Ring &state = ring();
    const QMutexLocker lock(&state.mutex);
    return {state.lines.begin(), state.lines.end()};
}

QString LogRing::formatLine(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    const QString when = QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
    const QString category = context.category != nullptr ? QString::fromUtf8(context.category) : u"default"_s;
    return when + u' ' + levelName(type) + u' ' + category + u": "_s + message;
}

void LogRing::uninstallForTesting()
{
    Ring &state = ring();
    const QMutexLocker lock(&state.mutex);
    if (state.installed) {
        qInstallMessageHandler(state.previous);
    }
    state.installed = false;
    state.previous = nullptr;
    state.lines.clear();
    state.capacity = kDefaultCapacity;
}

} // namespace rb::core

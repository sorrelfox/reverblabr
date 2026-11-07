// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "restart.h"

#include "language.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLoggingCategory>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace
{

Q_LOGGING_CATEGORY(lcRestart, "reverblabr.restart", QtInfoMsg)

/// How often the copy looks whether the old process has gone.
constexpr auto kPollInterval = std::chrono::milliseconds(50);

rb::ui::Restart::Runner &testRunner()
{
    static rb::ui::Restart::Runner runner;
    return runner;
}

/// What the kernel says of a process: whether it is there, whether it has
/// exited and is only waiting for its parent to collect it, and its name.
struct ProcessState {
    bool exists = false;
    bool exited = false;
    QByteArray name;
};

ProcessState stateOf(qint64 pid)
{
    QFile stat(u"/proc/%1/stat"_s.arg(pid));
    if (!stat.open(QIODevice::ReadOnly)) {
        return {};
    }
    // "1234 (reverblabr) S …": the name is between the first parenthesis and
    // the last, since it may hold one itself, and the state follows it.
    const QByteArray line = stat.readAll();
    const qsizetype open = line.indexOf('(');
    const qsizetype close = line.lastIndexOf(')');
    if (open < 0 || close < open || close + 2 >= line.size()) {
        return {.exists = true, .exited = false, .name = {}};
    }
    const char state = line.at(close + 2);
    // **A zombie has gone**: it has closed its bus connection, its tray icon
    // and everything else, and only its exit status is left for its parent.
    return {.exists = true, .exited = state == 'Z' || state == 'X', .name = line.mid(open + 1, close - open - 1)};
}

} // namespace

namespace rb::ui
{

Restart::Restart(QObject *parent)
    : QObject(parent)
{
}

bool Restart::failed() const
{
    return m_failed;
}

bool Restart::now()
{
    const QString program = QCoreApplication::applicationFilePath();
    const QStringList arguments = argumentsFor(QCoreApplication::arguments(), QCoreApplication::applicationPid());
    const QProcessEnvironment environment = environmentFor(QProcessEnvironment::systemEnvironment());
    bool started = false;
    if (testRunner()) {
        started = testRunner()(program, arguments, environment);
    } else {
        QProcess process;
        process.setProgram(program);
        process.setArguments(arguments);
        process.setProcessEnvironment(environment);
        process.setStandardInputFile(QProcess::nullDevice());
        started = process.startDetached();
    }
    if (m_failed == started) {
        m_failed = !started;
        Q_EMIT failedChanged();
    }
    if (!started) {
        qCWarning(lcRestart) << "could not start" << program << "again";
        return false;
    }
    qCInfo(lcRestart) << "started again with" << arguments;
    // **Once this call has returned**: the button that asked is still in
    // the middle of its click.
    if (!testRunner()) {
        QTimer::singleShot(0, QCoreApplication::instance(), [] { QCoreApplication::quit(); });
    }
    return true;
}

QStringList Restart::argumentsFor(const QStringList &arguments, qint64 pid)
{
    // **Read by hand, as LaunchRequests reads its own**: the options that
    // chose the engine are flags, and everything else is left behind.
    static const QStringList engineOptions{u"--mock"_s, u"--in-process"_s, u"--daemon"_s};
    QStringList kept;
    for (qsizetype index = 1; index < arguments.size(); ++index) {
        const QString &argument = arguments.at(index);
        // After "--" everything is a file name, never an option.
        if (argument == "--"_L1) {
            break;
        }
        if (engineOptions.contains(argument) && !kept.contains(argument)) {
            kept.append(argument);
        }
        // **An option's value is not an option**, whatever it says.
        if (argument == "--open-session"_L1) {
            ++index;
        }
    }
    kept.append(u"--%1=%2"_s.arg(kRestartOfOption).arg(pid));
    return kept;
}

QProcessEnvironment Restart::environmentFor(QProcessEnvironment environment)
{
    environment = notify::ProcessLanguage::restored(std::move(environment));
    environment.remove(u"XDG_ACTIVATION_TOKEN"_s);
    environment.remove(u"DESKTOP_STARTUP_ID"_s);
    return environment;
}

bool Restart::waitForExitOf(qint64 pid, std::chrono::milliseconds limit)
{
    const qint64 own = ::getpid();
    if (pid <= 1 || pid == own) {
        return true;
    }
    const QByteArray name = stateOf(own).name;
    QElapsedTimer clock;
    clock.start();
    for (;;) {
        const ProcessState state = stateOf(pid);
        if (!state.exists || state.exited || state.name != name) {
            return true;
        }
        if (clock.durationElapsed() >= limit) {
            qCWarning(lcRestart) << "the Reverblabr this one replaces, process" << pid << "is still there after"
                                 << limit.count() << "ms";
            return false;
        }
        QThread::sleep(kPollInterval);
    }
}

void Restart::setRunnerForTesting(Runner runner)
{
    testRunner() = std::move(runner);
}

} // namespace rb::ui

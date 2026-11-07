// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QLatin1StringView>
#include <QObject>
#include <QProcessEnvironment>
#include <QQmlEngine>
#include <QStringList>

#include <chrono>
#include <functional>

namespace rb::ui
{

/// **Reverblabr started again, for what only a start can change**
/// (ADR-0036): the language, which every part of the window takes once as
/// it starts, and the control style, which Qt cannot change in a window that
/// is open (Appearance). The Settings window's Restart Now.
///
/// - **A copy of the program is started first, on its own**, with the
///   environment this one was started with (LANGUAGE as it was before the
///   person's choice was put in it, notify::ProcessLanguage) and only the
///   options that chose the engine: a session to open or the Sessions window
///   was this launch's request, not something to do again.
/// - **Then this one quits**, as the tray's Quit does, so that what quitting
///   writes down is written.
/// - **The copy waits for this one to have gone** (waitForExitOf(), from
///   main(), before it takes Reverblabr's name on the bus), so that the name,
///   the tray icon and the global shortcuts are free when it asks for them:
///   one Reverblabr a desktop holds the whole time.
///
/// A window that runs its own engine stops its sessions with it; the
/// Settings window says so beside the button.
class Restart : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Whether the last Restart Now could not start the copy: this one goes
    /// on, and says so.
    Q_PROPERTY(bool failed READ failed NOTIFY failedChanged)

public:
    /// The hidden option that tells the copy which process it replaces:
    /// `--restart-of=<pid>`.
    static constexpr QLatin1StringView kRestartOfOption{"restart-of"};
    /// How long the copy waits for the old process at most. **Long enough
    /// for an engine of the window's own to close its sessions**; a process
    /// still there after it would hand the copy's launch to itself, and the
    /// copy would end.
    static constexpr std::chrono::milliseconds kWaitLimit{10000};

    explicit Restart(QObject *parent = nullptr);

    [[nodiscard]] bool failed() const;

    /// Starts the copy and quits this one, once the call has returned.
    /// False, with nothing quitting, when the copy could not be started.
    Q_INVOKABLE bool now();

    /// **What the copy is started with**: of \a arguments, a whole command
    /// line with the program first, only `--mock`, `--in-process` and
    /// `--daemon`, then `--restart-of=` and \a pid.
    [[nodiscard]] static QStringList argumentsFor(const QStringList &arguments, qint64 pid);

    /// **The environment the copy gets**: \a environment with LANGUAGE as it
    /// was when this process started, and without the activation token this
    /// launch came with, which the compositor has taken already.
    [[nodiscard]] static QProcessEnvironment environmentFor(QProcessEnvironment environment);

    /// **Waits, without signalling it, until \a pid has gone**: while it is a
    /// process of this program's name, looked at about every 50 ms, for no
    /// longer than \a limit. A pid that is not one, is this process, or is
    /// some other program's is not waited for. Answers whether the process
    /// had gone by then.
    static bool waitForExitOf(qint64 pid, std::chrono::milliseconds limit = kWaitLimit);

    /// Starts \a program with \a arguments and \a environment on its own.
    using Runner = std::function<
        bool(const QString &program, const QStringList &arguments, const QProcessEnvironment &environment)>;
    /// **A test's way of restarting**: the copy is started with \a runner
    /// instead, and nothing quits. Empty for the real thing.
    static void setRunnerForTesting(Runner runner);

Q_SIGNALS:
    void failedChanged();

private:
    bool m_failed = false;
};

} // namespace rb::ui

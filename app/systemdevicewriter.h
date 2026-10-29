// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QLatin1StringView>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QWindow>

#include <functional>

namespace rb::ui
{

/// Asks the all-users helper to write the system file (ADR-0013),
/// which shows the desktop's administrator password prompt.
///
/// **One request at a time.** The helper is started by the system bus, the
/// user answers a prompt, and the file is written; a second request while
/// the first is out would race it for the same file. write() refuses one.
///
/// The request itself goes to a *runner*. The real one uses KAuth; a test,
/// which has no system bus and no polkit to ask, gives its own with
/// setRunner() — or, for a whole QML test run, setRunnerForTesting().
class SystemDeviceWriter : public QObject
{
    Q_OBJECT

public:
    /// The helper's answer: empty when the file was written, or the reason
    /// it was not. The reasons are a pw::VirtualDeviceError's name,
    /// "BadRequest" or "ChangedElsewhere" from the helper, or one of the
    /// k* reasons below.
    using Done = std::function<void(const QString &reason)>;
    /// Sends the helper action's arguments, and calls done exactly once.
    using Runner = std::function<void(const QVariantMap &arguments, QWindow *promptParent, Done done)>;

    /// The password prompt was dismissed.
    static constexpr QLatin1StringView kCancelled{"AuthorizationCancelled"};
    /// The password was wrong, or the user may not be an administrator.
    static constexpr QLatin1StringView kDenied{"AuthorizationDenied"};
    /// The helper is not installed, or the system bus would not start it:
    /// the build has no KAuth, or the helper's files are not in place.
    static constexpr QLatin1StringView kUnavailable{"HelperUnavailable"};
    /// The helper started and failed without saying why.
    static constexpr QLatin1StringView kFailed{"HelperFailed"};

    /// Every reason of this class's own, so that a test can hold the
    /// interface to having words for each.
    [[nodiscard]] static QStringList reasons();

    /// Whether this build can ask the helper at all (KAuth was there).
    [[nodiscard]] static bool available();

    explicit SystemDeviceWriter(QObject *parent = nullptr);
    ~SystemDeviceWriter() override;

    /// Sends \a arguments — core::SystemDeviceWrite::helperArguments() —
    /// and reports on finished(). False, and nothing sent, while a request
    /// is still out.
    bool write(const QVariantMap &arguments);

    [[nodiscard]] bool busy() const;

    /// The window the password prompt belongs to.
    void setPromptParent(QWindow *window);

    /// This writer's runner; an empty one goes back to the default.
    void setRunner(Runner runner);

    /// The runner every writer made from now on starts with, in place of
    /// KAuth: for a whole test run (tests/qml). An empty one goes back.
    static void setRunnerForTesting(Runner runner);

Q_SIGNALS:
    void busyChanged();
    /// The request is answered: \a reason is empty when the file was
    /// written.
    void finished(const QString &reason);

private:
    Runner m_runner;
    QPointer<QWindow> m_promptParent;
    bool m_busy = false;
};

} // namespace rb::ui

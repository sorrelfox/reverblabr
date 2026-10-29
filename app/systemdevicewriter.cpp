// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "systemdevicewriter.h"

#include "helper/systemdevicesaction.h"

#include <QCoreApplication>
#include <QMetaObject>

#if REVERBLABR_KAUTH
#include <KAuth/Action>
#include <KAuth/ActionReply>
#include <KAuth/ExecuteJob>
#endif

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

SystemDeviceWriter::Runner &testingRunner()
{
    static SystemDeviceWriter::Runner runner;
    return runner;
}

#if REVERBLABR_KAUTH

/// The helper's answer, in the words the interface has for it.
[[nodiscard]] QString reasonFor(const KAuth::ExecuteJob &job)
{
    if (job.error() == 0) {
        return {};
    }
    // The helper's own refusal says why in its reply's data; KAuth's own
    // errors carry none.
    if (const QString reason = job.data().value(QString(helper::kReasonKey)).toString(); !reason.isEmpty()) {
        return reason;
    }
    switch (job.error()) {
    case KAuth::ActionReply::UserCancelledError:
        return QString(SystemDeviceWriter::kCancelled);
    case KAuth::ActionReply::AuthorizationDeniedError:
        return QString(SystemDeviceWriter::kDenied);
    case KAuth::ActionReply::NoResponderError:
    case KAuth::ActionReply::NoSuchActionError:
    case KAuth::ActionReply::InvalidActionError:
    case KAuth::ActionReply::DBusError:
    case KAuth::ActionReply::BackendError:
        return QString(SystemDeviceWriter::kUnavailable);
    default:
        return QString(SystemDeviceWriter::kFailed);
    }
}

void runWithKAuth(const QVariantMap &arguments, QWindow *promptParent, const SystemDeviceWriter::Done &done)
{
    KAuth::Action action{QString(helper::kWriteAction)};
    action.setHelperId(QString(helper::kHelperId));
    action.setArguments(arguments);
    if (promptParent) {
        action.setParentWindow(promptParent);
    }
    KAuth::ExecuteJob *job = action.execute();
    QObject::connect(job, &KJob::result, job, [done](KJob *finished) {
        done(reasonFor(*static_cast<KAuth::ExecuteJob *>(finished)));
    });
    job->start();
}

#endif

void runDefault(const QVariantMap &arguments, QWindow *promptParent, const SystemDeviceWriter::Done &done)
{
#if REVERBLABR_KAUTH
    runWithKAuth(arguments, promptParent, done);
#else
    Q_UNUSED(arguments)
    Q_UNUSED(promptParent)
    // Answered after write() returns, like every other answer.
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [done] { done(QString(SystemDeviceWriter::kUnavailable)); },
        Qt::QueuedConnection);
#endif
}

} // namespace

QStringList SystemDeviceWriter::reasons()
{
    return {QString(kCancelled), QString(kDenied), QString(kUnavailable), QString(kFailed)};
}

bool SystemDeviceWriter::available()
{
    return REVERBLABR_KAUTH != 0;
}

SystemDeviceWriter::SystemDeviceWriter(QObject *parent)
    : QObject(parent)
    , m_runner(testingRunner())
{
}

SystemDeviceWriter::~SystemDeviceWriter() = default;

bool SystemDeviceWriter::write(const QVariantMap &arguments)
{
    if (m_busy) {
        return false;
    }
    m_busy = true;
    Q_EMIT busyChanged();

    // Answered at most once, and only while this writer is still here.
    const QPointer<SystemDeviceWriter> self(this);
    const Done done = [self](const QString &reason) {
        if (!self || !self->m_busy) {
            return;
        }
        self->m_busy = false;
        Q_EMIT self->busyChanged();
        Q_EMIT self->finished(reason);
    };
    if (m_runner) {
        m_runner(arguments, m_promptParent, done);
    } else {
        runDefault(arguments, m_promptParent, done);
    }
    return true;
}

bool SystemDeviceWriter::busy() const
{
    return m_busy;
}

void SystemDeviceWriter::setPromptParent(QWindow *window)
{
    m_promptParent = window;
}

void SystemDeviceWriter::setRunner(Runner runner)
{
    m_runner = std::move(runner);
}

void SystemDeviceWriter::setRunnerForTesting(Runner runner)
{
    testingRunner() = std::move(runner);
}

} // namespace rb::ui

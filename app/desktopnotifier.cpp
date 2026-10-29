// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "desktopnotifier.h"

#include "core/enginefacade.h"

#include <KNotification>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QUrl>

namespace rb::ui
{

DesktopNotifier::DesktopNotifier(core::EngineFacade *engine, QObject *parent)
    : QObject(parent)
{
    connect(engine, &core::EngineFacade::error, &m_policy, &notify::NotificationPolicy::error);
    connect(engine, &core::EngineFacade::notice, &m_policy, &notify::NotificationPolicy::notice);
    connect(engine,
            &core::EngineFacade::recordingFileFinished,
            &m_policy,
            &notify::NotificationPolicy::recordingFileFinished);
    connect(&m_policy, &notify::NotificationPolicy::notify, this, &DesktopNotifier::send);
    // Queued behind any other window, and given up when this one goes.
    if (QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface(); bus != nullptr) {
        (void)bus->registerService(QString::fromLatin1(notify::kWindowNotifierName.data(),
                                                       static_cast<qsizetype>(notify::kWindowNotifierName.size())),
                                   QDBusConnectionInterface::QueueService,
                                   QDBusConnectionInterface::DontAllowReplacement);
    }
}

DesktopNotifier::~DesktopNotifier()
{
    if (QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface(); bus != nullptr) {
        (void)bus->unregisterService(QString::fromLatin1(notify::kWindowNotifierName.data(),
                                                         static_cast<qsizetype>(notify::kWindowNotifierName.size())));
    }
}

void DesktopNotifier::send(const notify::Notification &notification)
{
    // Deletes itself once it is closed, and goes with the window at the
    // latest: a server that never closes it must not keep it for ever.
    auto *sent = new KNotification(notify::eventId(notification.event), KNotification::CloseOnTimeout, this);
    sent->setTitle(notification.title);
    sent->setText(notification.text);
    if (!notification.file.isEmpty()) {
        const QString file = notification.file;
        // The file itself, which the desktop can show and let be dragged.
        sent->setUrls({QUrl::fromLocalFile(file)});
        KNotificationAction *clicked = sent->addDefaultAction(notify::openActionText());
        connect(clicked, &KNotificationAction::activated, this, [this, file] { m_files.open(file); });
        KNotificationAction *open = sent->addAction(notify::openActionText());
        connect(open, &KNotificationAction::activated, this, [this, file] { m_files.open(file); });
        KNotificationAction *show = sent->addAction(notify::showInFolderActionText());
        connect(show, &KNotificationAction::activated, this, [this, file] { m_files.showInFolder(file); });
    }
    if (notification.event == notify::Event::LowDiskSpace) {
        sent->setUrgency(KNotification::HighUrgency);
    }
    sent->sendEvent();
}

} // namespace rb::ui

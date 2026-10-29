// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "freedesktopsender.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QLoggingCategory>
#include <QProcess>
#include <QUrl>
#include <QVariantMap>

using namespace Qt::StringLiterals;

Q_LOGGING_CATEGORY(lcNotify, "reverblabr.notify")

namespace rb::notify
{

namespace
{

const QString kService = u"org.freedesktop.Notifications"_s;
const QString kPath = u"/org/freedesktop/Notifications"_s;
const QString kInterface = u"org.freedesktop.Notifications"_s;
const QString kDesktopEntry = u"io.github.reverblabr.Reverblabr"_s;

} // namespace

FreedesktopSender::FreedesktopSender(const QDBusConnection &bus, std::function<bool(Event)> wanted, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_wanted(std::move(wanted))
{
    m_opener = [bus](const QString &action, const QString &file) {
        if (action == "open"_L1) {
            QProcess::startDetached(u"xdg-open"_s, {file});
        } else {
            QDBusMessage show = QDBusMessage::createMethodCall(u"org.freedesktop.FileManager1"_s,
                                                               u"/org/freedesktop/FileManager1"_s,
                                                               u"org.freedesktop.FileManager1"_s,
                                                               u"ShowItems"_s);
            show.setArguments({QStringList{QUrl::fromLocalFile(file).toString()}, QString()});
            (void)QDBusConnection(bus).asyncCall(show);
        }
    };
    // **Not on a bus that is gone**: connect() asks the bus who owns the
    // notification server, and on a connection that has lost its bus,
    // libdbus ends the process for it. The bus can go while the daemon
    // starts (a logout at that moment), and then there is nothing to hear.
    if (!m_bus.isConnected()) {
        return;
    }
    m_bus.connect(kService, kPath, kInterface, u"ActionInvoked"_s, this, SLOT(actionInvoked(uint, QString)));
    m_bus.connect(kService, kPath, kInterface, u"NotificationClosed"_s, this, SLOT(notificationClosed(uint, uint)));
}

FreedesktopSender::~FreedesktopSender() = default;

void FreedesktopSender::setOpenerForTesting(Opener opener)
{
    m_opener = std::move(opener);
}

void FreedesktopSender::send(const Notification &notification)
{
    if (!m_wanted(notification.event)) {
        return;
    }
    QStringList actions;
    if (!notification.file.isEmpty()) {
        // Pairs of key and words; "default" is a click on the notification.
        actions = {u"default"_s, openActionText(), u"open"_s, openActionText(), u"show"_s, showInFolderActionText()};
    }
    QVariantMap hints{{u"desktop-entry"_s, kDesktopEntry}};
    if (notification.event == Event::LowDiskSpace) {
        hints.insert(u"urgency"_s, QVariant::fromValue(uchar(2)));
    }
    QDBusMessage notify = QDBusMessage::createMethodCall(kService, kPath, kInterface, u"Notify"_s);
    notify.setArguments({
        u"Reverblabr"_s, // the application's name, which is not translated
        uint(0),
        kDesktopEntry,
        notification.title,
        notification.text,
        actions,
        hints,
        int(-1),
    });
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(notify), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, file = notification.file](auto *call) {
        const QDBusPendingReply<uint> reply = *call;
        call->deleteLater();
        if (reply.isError()) {
            qCWarning(lcNotify) << "the notification was not shown:" << reply.error().message();
            return;
        }
        if (!file.isEmpty()) {
            m_files.insert(reply.value(), file);
        }
    });
}

void FreedesktopSender::actionInvoked(uint id, const QString &action)
{
    const QString file = m_files.value(id);
    if (file.isEmpty()) {
        return;
    }
    m_opener(action == "show"_L1 ? u"show"_s : u"open"_s, file);
}

void FreedesktopSender::notificationClosed(uint id, uint reason)
{
    Q_UNUSED(reason)
    m_files.remove(id);
}

} // namespace rb::notify

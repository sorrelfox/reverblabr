// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "notifications.h"

#include <QDBusConnection>
#include <QHash>
#include <QObject>

#include <functional>

namespace rb::notify
{

/// **The daemon's notifications**: straight to the desktop's
/// `org.freedesktop.Notifications` server, for an engine that has no
/// KNotifications (ADR-0017) and no window to send them through.
///
/// A finished recording offers Open and Show in Folder. **Opening is the
/// desktop's**: `xdg-open` for the file, the file manager's `FileManager1`
/// interface for its folder, as the engine's openUrlRequested() already
/// leaves to whoever holds the desktop.
class FreedesktopSender : public QObject
{
    Q_OBJECT

public:
    /// What an action does: \a action is "open" or "show", \a file the
    /// recording.
    using Opener = std::function<void(const QString &action, const QString &file)>;

    /// Sends on \a bus. \a wanted says, for each notification, whether it is
    /// this sender's to send at all — the daemon's answer is "only while no
    /// window is connected", since a window sends its own.
    FreedesktopSender(const QDBusConnection &bus, std::function<bool(Event)> wanted, QObject *parent = nullptr);
    ~FreedesktopSender() override;

    /// Replaces what the actions do, for a test.
    void setOpenerForTesting(Opener opener);

public Q_SLOTS:
    void send(const rb::notify::Notification &notification);

private Q_SLOTS:
    void actionInvoked(uint id, const QString &action);
    void notificationClosed(uint id, uint reason);

private:
    QDBusConnection m_bus;
    std::function<bool(Event)> m_wanted;
    Opener m_opener;
    /// The recording each notification with actions is about, by the
    /// server's id for it.
    QHash<uint, QString> m_files;
};

} // namespace rb::notify

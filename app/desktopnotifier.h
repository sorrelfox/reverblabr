// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "fileactions.h"
#include "notifications.h"

#include <QObject>

namespace rb::core
{
class EngineFacade;
}

namespace rb::ui
{

/// **The window's desktop notifications**: what the engine says that
/// is worth one — a recording finished, a device gone, an application quiet,
/// a plugin crashed, a disk filling up, a feedback loop refused — sent
/// through KNotifications, so that each is an event of `reverblabr.notifyrc`
/// that a person can turn off, give a sound or keep quiet under Do Not
/// Disturb in System Settings.
///
/// What is worth sending, and the words, are rb::notify's, shared with the
/// daemon, which sends the same ones itself while no window is open. **It
/// holds notify::kWindowNotifierName on the session bus for as long as it
/// lives**, which is how the daemon knows a window is saying them.
class DesktopNotifier : public QObject
{
    Q_OBJECT

public:
    /// Follows \a engine, which must outlive this.
    explicit DesktopNotifier(core::EngineFacade *engine, QObject *parent = nullptr);
    ~DesktopNotifier() override;

private:
    void send(const notify::Notification &notification);

    notify::NotificationPolicy m_policy;
    FileActions m_files;
};

} // namespace rb::ui

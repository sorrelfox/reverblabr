// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "applicationicon.h"

#include <QDBusConnection>
#include <QObject>
#include <QString>

#include <memory>

class KStatusNotifierItem;

namespace rb::ui
{

class TrayState;

/// **Reverblabr's icon in the system tray**: a KStatusNotifierItem
/// that shows what TrayState says and hands its clicks back to it.
///
/// - **Left click** asks for the popover (TrayState::popoverRequested). The
///   item has no associated window, so KStatusNotifierItem's own habit of
///   toggling the main window on a click is not what happens.
/// - **Middle click** shows or hides the main window.
/// - **The menu** has Show Reverblabr and All Sessions… above the standard
///   Quit.
/// - **Status**: Passive while nothing runs (the tray keeps it among its
///   hidden items), Active while a session runs or a Recorder writes, and
///   NeedsAttention when something went wrong that nobody has seen.
/// - **A red record overlay** while any Recorder writes a file.
///
/// **Made only by the application's main()**: a test that loads the window
/// gets TrayState and no icon, so nothing ever lands in a desktop's tray
/// from a test run. The icon's own test runs on a private bus with a
/// StatusNotifierWatcher of the test's own (tests/bus).
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    /// The icon's name: the desktop entry's `Icon=`, so the tray and the
    /// launcher show the same picture. Before Reverblabr is installed
    /// the tray is given the picture itself instead.
    static constexpr QLatin1StringView kIconName = kApplicationIconName;
    /// The overlay while recording, and the icon while attention is asked.
    static constexpr QLatin1StringView kRecordingOverlay{"media-record"};
    static constexpr QLatin1StringView kAttentionIcon{"dialog-warning"};

    /// Shows \a state, which must outlive this, and tells it whether there is
    /// a tray to be shown in, now and whenever that changes.
    explicit TrayIcon(TrayState *state, QObject *parent = nullptr);
    ~TrayIcon() override;

    /// **Whether a tray will show an icon**: a StatusNotifierWatcher on \a bus
    /// with a host registered with it. Without one, an item is registered
    /// with nobody and a hidden window could never be brought back.
    [[nodiscard]] static bool hostAvailable(const QDBusConnection &bus);

    /// The item itself, for a test to read.
    [[nodiscard]] KStatusNotifierItem *item() const;

private Q_SLOTS:
    /// The watcher says a host came or went: whether there is a tray is
    /// asked again.
    void hostsChanged();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui

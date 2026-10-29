// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QAction;

namespace rb::ui
{

class GlobalShortcuts;

/// **Reverblabr's global shortcuts, registered with KGlobalAccel**, so
/// they are in System Settings → Shortcuts under Reverblabr and work while
/// the window is in the background or hidden in the tray.
///
/// Each of GlobalShortcuts' actions becomes a QAction whose object name is
/// the action's id, registered with **no default key**: the person picks
/// one. A press comes back as GlobalShortcuts::trigger().
///
/// - **A renamed session's action is registered again** under the same id
///   with its new name. The old QAction goes first, which KGlobalAccel takes
///   as "inactive" and keeps the key for, and the new one loads it back.
/// - **A deleted session's action is removed with its key**
///   (KGlobalAccel::removeAllShortcuts), so it does not linger in the
///   settings for a session that is gone.
///
/// **Made only by the application's main()**, like TrayIcon: a test that
/// loads the window gets GlobalShortcuts and no registry, so nothing is ever
/// bound on a desktop from a test run. The registry's own test runs on a
/// private bus against a kglobalaccel of the test's own (tests/bus).
class GlobalShortcutRegistry : public QObject
{
    Q_OBJECT

public:
    /// Registers \a shortcuts' actions, which must outlive this, and follows
    /// them as sessions come, go and are renamed.
    explicit GlobalShortcutRegistry(GlobalShortcuts *shortcuts, QObject *parent = nullptr);
    ~GlobalShortcutRegistry() override;

    /// The action registered under \a actionId, or null: for a test.
    [[nodiscard]] QAction *action(const QString &actionId) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui

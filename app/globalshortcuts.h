// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

#include <memory>

namespace rb::ui
{

/// **What Reverblabr's global shortcuts are, and what each one does**:
/// shortcuts that work while the window is in the background or hidden in
/// the tray, set by the person in System Settings → Shortcuts.
///
/// **No desktop in here.** GlobalShortcutRegistry turns these into
/// KGlobalAccel actions and passes their presses back to trigger(), so
/// everything a shortcut decides can be tested against the mock engine with
/// no kglobalaccel, no bus and no window.
///
/// **What each one acts on:**
/// - **A shortcut of its own for every saved session**, "Run or stop
///   “Podcast”": it follows the session rather than a place in a list, so
///   reordering or adding sessions never moves a binding onto another one.
///   A session only on disk is opened and started, as the Sessions window
///   does. Deleting the session deletes its shortcut; renaming it keeps it.
/// - **Show Reverblabr** brings the main window to the front.
/// - **The block shortcuts act on the front session**, the one whose tab the
///   main window shows (or showed last, while it is hidden), and on every
///   block of that kind in it, which is usually one: Input Switch to A or
///   B; Time Shift pause or play, back and forward 10 seconds, back to live;
///   Recorder, start a new file (only a Recorder that is recording). A
///   session with no such block, or no session open, does nothing. The
///   window's QML does these (ShortcutTargets.qml), since it is what knows
///   the front session and its blocks' live readings.
///
/// **No default keys.** Any key chosen here would clash with something on
/// somebody's desktop, and Plasma's own shortcuts cover most of the easy
/// ones; the person picks them. The actions are there, unbound, from the
/// first start.
class GlobalShortcuts : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Every action, as `[{id, text}]`: the fixed ones, then one per saved
    /// session in the order the sessions are listed.
    Q_PROPERTY(QVariantList actions READ actionList NOTIFY actionsChanged)
    /// Whether the saved sessions have been listed, so the per-session
    /// actions are all there.
    Q_PROPERTY(bool ready READ isReady NOTIFY actionsChanged)

public:
    /// One action: the id KGlobalAccel keeps its shortcut under, which never
    /// changes, and what System Settings shows for it.
    struct Action {
        QString id;
        QString text;

        friend bool operator==(const Action &, const Action &) = default;
    };

    static constexpr QLatin1StringView kShowWindow{"window.show"};
    static constexpr QLatin1StringView kInputSwitchA{"inputSwitch.a"};
    static constexpr QLatin1StringView kInputSwitchB{"inputSwitch.b"};
    static constexpr QLatin1StringView kTimeShiftPause{"timeShift.pause"};
    static constexpr QLatin1StringView kTimeShiftBack{"timeShift.back"};
    static constexpr QLatin1StringView kTimeShiftForward{"timeShift.forward"};
    static constexpr QLatin1StringView kTimeShiftLive{"timeShift.live"};
    static constexpr QLatin1StringView kRecorderSplit{"recorder.split"};
    /// A session's action is this followed by its id.
    static constexpr QLatin1StringView kSessionPrefix{"session."};
    /// How far the Time Shift shortcuts jump, in seconds.
    static constexpr int kJumpSeconds = 10;

    /// Follows \a facade (see attach()). **Not default-constructible, on
    /// purpose**, like TrayState: QML would otherwise make a second one of
    /// its own, and the registry and the window would be listening to
    /// different ones.
    explicit GlobalShortcuts(core::EngineFacade *facade, QObject *parent = nullptr);
    ~GlobalShortcuts() override;

    /// The one the registry and the window's QML share.
    [[nodiscard]] static GlobalShortcuts *instance();
    static GlobalShortcuts *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade instead, which must outlive this, and lists its
    /// sessions again.
    void attach(core::EngineFacade *facade);

    /// The actions that are always there, in the order System Settings
    /// shows them.
    [[nodiscard]] static QList<Action> fixedActions();
    /// The session a session's action is for, or empty for any other.
    [[nodiscard]] static QString sessionOf(const QString &actionId);

    [[nodiscard]] QList<Action> actions() const;
    [[nodiscard]] QVariantList actionList() const;
    [[nodiscard]] bool isReady() const;

    /// **A shortcut was pressed.** A session's runs it or stops it; Show
    /// Reverblabr and the block actions are passed on (showRequested(),
    /// blockActionRequested()). An id this does not know does nothing.
    Q_INVOKABLE void trigger(const QString &actionId);

Q_SIGNALS:
    /// The actions came or went, or one was renamed.
    void actionsChanged();
    /// \a actionId's session was deleted: its shortcut goes with it, rather
    /// than waiting in the settings for a session that will not come back.
    /// Said before actionsChanged(). Never said when the list comes back
    /// empty, which is as likely a folder that could not be read.
    void forgotten(const QString &actionId);
    /// Show Reverblabr: the main window comes to the front.
    void showRequested();
    /// One of the block actions, for the front session's blocks.
    void blockActionRequested(const QString &actionId);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui

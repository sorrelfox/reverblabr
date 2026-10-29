// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/enginefacade.h"

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// **What the system tray says about Reverblabr**, and what a click on
/// it asks for: how many sessions run, whether anything records, whether
/// something went wrong that nobody has looked at yet, and the meters a
/// person asked to see there.
///
/// **No desktop in here.** The icon itself (TrayIcon, a KStatusNotifierItem)
/// only shows what this says and passes its clicks back in, so everything the
/// tray decides can be tested against the mock engine with no tray host, no
/// bus and no window — and a QML test that loads the main window never puts
/// an icon in anybody's tray.
///
/// **From the engine's own words**: the session list and state changes for
/// what runs, the Recorders' file signals for what records, the same
/// NotificationPolicy the notifications use for what deserves attention, and
/// each open session's snapshot and patches for the meters marked "Show in
/// system tray".
class TrayState : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// How many sessions are running.
    Q_PROPERTY(int runningCount READ runningCount NOTIFY changed)
    /// Whether any Recorder is writing a file now: the red dot on the icon.
    Q_PROPERTY(bool recording READ isRecording NOTIFY changed)
    /// Whether something went wrong that the person has not seen yet.
    Q_PROPERTY(bool attention READ needsAttention NOTIFY changed)
    /// What went wrong, in words, while attention is asked for.
    Q_PROPERTY(QString attentionText READ attentionText NOTIFY changed)
    /// kPassive, kActive or kAttention: the item's status.
    Q_PROPERTY(QString status READ status NOTIFY changed)
    /// The line under "Reverblabr" in the icon's tooltip.
    Q_PROPERTY(QString toolTip READ toolTip NOTIFY changed)
    /// The meters marked "Show in system tray" in every open session, as
    /// `[{sessionId, sessionName, blockId, type, nickname}]`, in the order
    /// the sessions were opened and the blocks sit.
    Q_PROPERTY(QVariantList meters READ meters NOTIFY metersChanged)
    /// Whether there is a tray to show the icon in. **Without one, closing
    /// the window must end the application**, or it would go on running with
    /// no way back to it. TrayIcon sets it as hosts come and go; a test sets
    /// it to stand in for one.
    Q_PROPERTY(bool available READ isAvailable WRITE setAvailable NOTIFY availableChanged)
    /// Whether the main window is on screen; the window sets it. **While it
    /// is not, the icon is the only way back**, so it is never passive then:
    /// a desktop tray hides passive icons behind its arrow.
    Q_PROPERTY(bool windowShown READ isWindowShown WRITE setWindowShown NOTIFY changed)
    /// Whether the meters' levels are read: only while the popover shows
    /// them.
    Q_PROPERTY(bool watching READ isWatching WRITE setWatching NOTIFY watchingChanged)
    /// Counts up whenever the levels were read, for bindings to depend on.
    Q_PROPERTY(qulonglong frame READ frame NOTIFY ticked)

public:
    static constexpr QLatin1StringView kPassive{"passive"};
    static constexpr QLatin1StringView kActive{"active"};
    static constexpr QLatin1StringView kAttention{"attention"};
    /// How fast a tray meter comes down after a peak, as every meter's does.
    static constexpr float kFallDbPerSecond = 24.0f;
    /// How often the levels are read while watching.
    static constexpr int kLevelTickMs = 50;

    /// Follows \a facade (see attach()). **Not default-constructible, on
    /// purpose**: QML would make a singleton of its own with a default
    /// constructor rather than call create(), and the icon and the window
    /// would then be talking to two different trays.
    explicit TrayState(core::EngineFacade *facade, QObject *parent = nullptr);
    ~TrayState() override;

    /// The one the window's QML and the tray icon share.
    [[nodiscard]] static TrayState *instance();
    static TrayState *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// Follows \a facade instead, which must outlive this, and asks it for
    /// the session list. A test gives its own; the application gives
    /// AppShell's.
    void attach(core::EngineFacade *facade);

    [[nodiscard]] int runningCount() const;
    [[nodiscard]] bool isRecording() const;
    [[nodiscard]] bool needsAttention() const;
    [[nodiscard]] QString attentionText() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] QString toolTip() const;
    [[nodiscard]] QVariantList meters() const;
    [[nodiscard]] bool isAvailable() const;
    void setAvailable(bool available);
    [[nodiscard]] bool isWindowShown() const;
    void setWindowShown(bool shown);
    [[nodiscard]] bool isWatching() const;
    void setWatching(bool watching);
    [[nodiscard]] qulonglong frame() const;

    /// **The person has looked**: what went wrong no longer asks for
    /// attention. A session that is still in error keeps asking, since
    /// looking does not mend it.
    Q_INVOKABLE void acknowledge();

    /// One tray meter's level now, as `{peakDb, active}`: the loudest of its
    /// channels, falling steadily after a peak, and whether there was any
    /// sound at all. Silence for a meter whose session does not run.
    [[nodiscard]] Q_INVOKABLE QVariantMap meterLevel(const QString &sessionId, const QString &blockId) const;

    /// What the icon's clicks ask for, said by TrayIcon and by tests.
    Q_INVOKABLE void requestPopover();
    Q_INVOKABLE void requestWindowToggle();
    Q_INVOKABLE void requestSessionsWindow();
    /// A meter in the popover was clicked: its session is to come forward.
    Q_INVOKABLE void bringForward(const QString &sessionId, const QString &blockId);

    /// Reads the levels once, now: what the timer does while watching, for a
    /// test that cannot wait for it.
    void readLevels();

Q_SIGNALS:
    void changed();
    void metersChanged();
    void availableChanged();
    void watchingChanged();
    void ticked();
    /// A left click: the popover opens, or closes if it is open.
    void popoverRequested();
    /// A middle click: the main window shows, or hides if it shows.
    void windowToggleRequested();
    /// "All Sessions…" in the icon's menu.
    void sessionsWindowRequested();
    void forwardRequested(const QString &sessionId, const QString &blockId);

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui

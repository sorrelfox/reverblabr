// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "telemetrywatch.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace rb::ui
{

class TrayState;

/// **What the small windows that float over other programs show, and where
/// they sit** (ADR-0030).
///
/// **Its own read, in every session.** `Meters` follows the one session the
/// main window shows, and the tray reads nothing while its popover is shut.
/// These windows stay up with the main window closed, over whichever
/// sessions have asked for them, so they read the telemetry themselves, as
/// long as there is a row to show. **Which blocks those are comes from
/// TrayState**, which already follows every open session, so that there is
/// one copy of them.
///
/// **A change is a change**: `rowsChanged` is said when a row would look
/// different, not on every read, so nothing repaints between words.
///
/// **Where they sit is this desktop's business**, in the state file with
/// the windows' sizes and not in a session (WindowState): a place on a screen
/// means nothing on the next computer. Two placements are kept. The label
/// stack is in pixels, as it is dragged. The strip is in percent of the
/// screen, since it follows the picture and passes clicks through.
class FloatingSurface : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The label's rows, at most kMostRows, as `[{sessionId, blockId, type,
    /// nickname, watch, enabled, state}]`, and `reading` and `marks` for a
    /// Voice Gauge.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY rowsChanged)
    /// How many rows there was no room for.
    Q_PROPERTY(int leftOut READ leftOut NOTIFY rowsChanged)
    /// Whether anything asks for the label. Both check boxes start off, so
    /// no window appears over somebody's screen unasked.
    Q_PROPERTY(bool wanted READ isWanted NOTIFY rowsChanged)
    /// Counts up when a placement changes, for bindings to depend on.
    Q_PROPERTY(int placementRevision READ placementRevision NOTIFY placementChanged)

public:
    /// How often the telemetry is read while a row exists. Speaking lasts at
    /// least the shortest Hold, 100 ms, so three reads see each one.
    static constexpr int kTickMs = 33;
    /// How many rows the label has room for.
    static constexpr int kMostRows = 4;
    /// The two placements.
    static constexpr QLatin1StringView kLabel{"label"};
    static constexpr QLatin1StringView kStrip{"strip"};

    /// Follows \a tray for its blocks. **Not default-constructible, on
    /// purpose**, like TrayState: QML would make a second one of its own.
    explicit FloatingSurface(TrayState *tray, QObject *parent = nullptr);
    ~FloatingSurface() override;

    [[nodiscard]] static FloatingSurface *instance();
    static FloatingSurface *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    [[nodiscard]] QVariantList rows() const;
    [[nodiscard]] int leftOut() const;
    [[nodiscard]] bool isWanted() const;
    [[nodiscard]] int placementRevision() const;

    /// Where \a surface sits, as `{left, top, unit, size, screen}`.
    /// `unit` is "px" or "percent"; `screen` is a screen's name, or empty for
    /// the one the main window is on.
    [[nodiscard]] Q_INVOKABLE QVariantMap placement(const QString &surface) const;
    /// Takes the keys of \a changes, within their range, and writes the
    /// placement down. With \a moving true it is only remembered, as a drag
    /// is a hundred moves and this is a file in a person's home directory;
    /// the drag's end writes it, with no \a changes.
    Q_INVOKABLE void setPlacement(const QString &surface, const QVariantMap &changes, bool moving = false);

    /// Reads the telemetry once, now: what the timer does, for a test that
    /// cannot wait for it.
    void read();

    /// The clock the frames are timed against, for a test.
    void setClock(std::function<qint64()> clock);

Q_SIGNALS:
    void rowsChanged();
    void placementChanged();

private:
    void rowsMoved();

    TrayState *m_tray;
    QVariantList m_rows;
    int m_leftOut = 0;
    bool m_wanted = false;
    int m_placementRevision = 0;
    QTimer m_ticker;
    QElapsedTimer m_sinceStart;
    std::function<qint64()> m_clock;
    QHash<QString, TelemetryWatch> m_watches;
    /// Where a drag has got to, by surface, until it is written down.
    QHash<QString, QVariantMap> m_moving;
};

} // namespace rb::ui

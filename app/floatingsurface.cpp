// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "floatingsurface.h"

#include "appshell.h"
#include "blocks/speechnow.h"
#include "blocks/voicegauge.h"
#include "traystate.h"
#include "windowstate.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::ui
{

FloatingSurface::FloatingSurface(TrayState *tray, QObject *parent)
    : QObject(parent)
    , m_tray(tray)
{
    m_sinceStart.start();
    m_ticker.setInterval(kTickMs);
    connect(&m_ticker, &QTimer::timeout, this, &FloatingSurface::read);
    connect(m_tray, &TrayState::metersChanged, this, &FloatingSurface::rowsMoved);
    rowsMoved();
}

FloatingSurface::~FloatingSurface() = default;

FloatingSurface *FloatingSurface::instance()
{
    static FloatingSurface *surface = new FloatingSurface(TrayState::instance());
    return surface;
}

FloatingSurface *FloatingSurface::create(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    // A test that makes a window of its own brings an engine of its own, and
    // TrayState moves to it here.
    TrayState::create(engine, scriptEngine);
    FloatingSurface *surface = instance();
    // **One for the window and the card**, which is why QML must not own it.
    QJSEngine::setObjectOwnership(surface, QJSEngine::CppOwnership);
    return surface;
}

QVariantList FloatingSurface::rows() const
{
    return m_rows;
}

int FloatingSurface::leftOut() const
{
    return m_leftOut;
}

bool FloatingSurface::isWanted() const
{
    return m_wanted;
}

QVariantMap FloatingSurface::strip() const
{
    return m_strip;
}

int FloatingSurface::placementRevision() const
{
    return m_placementRevision;
}

QVariantMap FloatingSurface::placement(const QString &surface) const
{
    const bool strip = surface == kStrip;
    if (const auto moving = m_moving.constFind(strip ? kStrip : kLabel); moving != m_moving.cend()) {
        return *moving;
    }
    return WindowState{}.floatingPlacement(strip ? kStrip : kLabel, strip ? u"percent"_s : u"px"_s);
}

void FloatingSurface::setPlacement(const QString &surface, const QVariantMap &changes, bool moving)
{
    const bool strip = surface == kStrip;
    QVariantMap written = placement(surface);
    for (auto change = changes.cbegin(); change != changes.cend(); ++change) {
        written.insert(change.key(), change.value());
    }
    const double most = strip ? 100.0 : std::numeric_limits<int>::max();
    written.insert(u"left"_s, std::clamp(written.value(u"left"_s).toDouble(), 0.0, most));
    written.insert(u"top"_s, std::clamp(written.value(u"top"_s).toDouble(), 0.0, most));
    if (moving) {
        m_moving.insert(strip ? kStrip : kLabel, written);
    } else {
        m_moving.remove(strip ? kStrip : kLabel);
        WindowState{}.setFloatingPlacement(strip ? kStrip : kLabel, written);
    }
    ++m_placementRevision;
    Q_EMIT placementChanged();
}

void FloatingSurface::setClock(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

void FloatingSurface::rowsMoved()
{
    const QList<TrayState::SurfaceBlock> strips = m_tray->stripBlocks();
    const QVariantMap strip = strips.isEmpty()
        ? QVariantMap{}
        : QVariantMap{{u"sessionId"_s, strips.first().sessionId}, {u"blockId"_s, strips.first().blockId}};
    if (strip != m_strip) {
        m_strip = strip;
        Q_EMIT stripChanged();
    }
    if (!m_tray->floatingBlocks().isEmpty()) {
        read();
        if (!m_ticker.isActive()) {
            m_ticker.start();
        }
        return;
    }
    m_ticker.stop();
    m_watches.clear();
    if (m_wanted || !m_rows.isEmpty()) {
        m_rows.clear();
        m_leftOut = 0;
        m_wanted = false;
        Q_EMIT rowsChanged();
    }
}

void FloatingSurface::read()
{
    const QList<TrayState::SurfaceBlock> wanted = m_tray->floatingBlocks();
    const qint64 now = m_clock ? m_clock() : m_sinceStart.elapsed();
    QVariantList rows;
    QSet<QString> sessions;
    QString session;
    std::optional<core::TelemetrySnapshot> snapshot;
    for (const TrayState::SurfaceBlock &block : wanted) {
        sessions.insert(block.sessionId);
        // **One read a session**, however many rows it has.
        if (block.sessionId != session) {
            session = block.sessionId;
            const core::TelemetrySlot *slot = AppShell::telemetryOf(session);
            snapshot = slot != nullptr ? std::optional(slot->load()) : std::nullopt;
            if (snapshot) {
                m_watches[session].saw(*snapshot, now);
            } else {
                m_watches.remove(session);
            }
        }
        if (rows.size() >= kMostRows) {
            continue;
        }
        const core::BlockTelemetry *published = snapshot ? TelemetryWatch::find(*snapshot, block.blockId) : nullptr;
        const auto watch = m_watches.constFind(session);
        const QString said = watch != m_watches.cend() ? QString(watch->stateOf(published != nullptr, now))
                                                       : QString(TelemetryWatch::kStopped);
        QVariantMap row{
            {u"sessionId"_s, block.sessionId},
            {u"blockId"_s, block.blockId},
            {u"type"_s, block.type},
            {u"nickname"_s, block.nickname},
            {u"watch"_s, said},
            {u"enabled"_s, published == nullptr || published->enabled},
            {u"state"_s,
             published != nullptr ? static_cast<int>(std::lround(published->values[blocks::SpeechNow::kStateValue]))
                                  : 0},
        };
        // Only for a block with a number: a tenth of an LU is the most a
        // person could see, and the voice likelihood of a Speech Now would
        // make every read a change.
        if (block.type == blocks::VoiceGauge::kTypeId) {
            const float reading =
                published != nullptr ? published->values[blocks::VoiceGauge::kReadingValue] : dsp::kSilentLufs;
            row.insert(u"reading"_s, std::round(static_cast<double>(reading) * 10.0) / 10.0);
            row.insert(u"marks"_s,
                       published != nullptr
                           ? static_cast<int>(std::lround(published->values[blocks::VoiceGauge::kMarksValue]))
                           : 0);
        }
        rows.append(row);
    }
    for (auto it = m_watches.begin(); it != m_watches.end();) {
        it = sessions.contains(it.key()) ? std::next(it) : m_watches.erase(it);
    }
    const int leftOut = static_cast<int>(wanted.size()) - static_cast<int>(rows.size());
    if (rows == m_rows && leftOut == m_leftOut && m_wanted == !wanted.isEmpty()) {
        return;
    }
    m_rows = rows;
    m_leftOut = leftOut;
    m_wanted = !wanted.isEmpty();
    Q_EMIT rowsChanged();
}

} // namespace rb::ui

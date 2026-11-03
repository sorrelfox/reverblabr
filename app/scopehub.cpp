// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scopehub.h"

#include "core/telemetry.h"
#include "dsp/scopetrace.h"

#include <QElapsedTimer>

#include <algorithm>
#include <cmath>

namespace rb::ui
{

namespace
{

constexpr double kDefaultRate = 48000.0;
/// How often a surface that holds is still looked at, for the state.
constexpr int kLeastTicksPerSecond = 10;

[[nodiscard]] double sinceTheHubStarted()
{
    static const QElapsedTimer running = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return static_cast<double>(running.nsecsElapsed()) * 1.0e-6;
}

} // namespace

ScopeFeed::ScopeFeed(ScopeHub *hub, const QString &blockId)
    : m_hub(hub)
    , m_blockId(blockId)
{
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(static_cast<int>(1000.0 / m_settings.refreshHz));
    connect(&m_timer, &QTimer::timeout, this, &ScopeFeed::tick);
    m_wantedSince = static_cast<qint64>(m_hub->nowMs());
}

ScopeFeed::~ScopeFeed() = default;

void ScopeFeed::tell(const void *surface, const ScopeSettings &settings, bool hold)
{
    std::erase_if(m_said, [surface](const Said &said) { return said.surface == surface; });
    m_said.push_back(Said{surface, settings, hold});
    setSettings(settings);
    setHold(hold);
}

void ScopeFeed::forgetSurface(const void *surface)
{
    if (std::erase_if(m_said, [surface](const Said &said) { return said.surface == surface; }) > 0 && !m_said.empty()) {
        setSettings(m_said.back().settings);
        setHold(m_said.back().hold);
    }
}

void ScopeFeed::setSettings(const ScopeSettings &settings)
{
    const bool slowerOrFaster = settings.refreshHz != m_settings.refreshHz;
    m_settings = settings;
    if (slowerOrFaster) {
        m_timer.setInterval(static_cast<int>(1000.0 / std::max(settings.refreshHz, 1.0)));
    }
}

void ScopeFeed::setHold(bool hold)
{
    if (hold == m_hold) {
        return;
    }
    m_hold = hold;
    if (!hold) {
        // Whoever is asked next gets the block in time for the next look.
        m_renewedAt = -1;
        m_wantedSince = static_cast<qint64>(m_hub->nowMs());
        m_movedAt = m_wantedSince;
    }
    Q_EMIT refreshed();
}

double ScopeFeed::arrivalsPerSecond() const noexcept
{
    const std::uint32_t frames = m_hub->m_meters->frames();
    const std::uint32_t rate = m_hub->m_meters->sampleRate();
    return frames > 0 ? static_cast<double>(rate) / frames : 0.0;
}

void ScopeFeed::start()
{
    m_timer.start();
}

void ScopeFeed::stop()
{
    m_timer.stop();
}

void ScopeFeed::forget()
{
    const std::uint64_t run = m_frame.run;
    m_frame = ScopeFrame{};
    m_frame.run = run + 1;
    m_seenSlot = false;
    m_slot = -1;
    m_lastHead.fill(0);
    m_wraps.fill(0);
    m_renewedAt = -1;
    m_wantedSince = static_cast<qint64>(m_hub->nowMs());
    setState(State::Waiting);
    Q_EMIT refreshed();
}

void ScopeFeed::setState(State state)
{
    if (state == m_state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged();
    Q_EMIT refreshed();
}

void ScopeFeed::read(const rt::ScopeSlot &slot, double cycleMs, std::uint32_t sampleRate)
{
    const double rate = sampleRate > 0 ? sampleRate : kDefaultRate;
    const double wanted = ScopeView::framesWanted(m_settings, rate, cycleMs);
    const std::size_t level = dsp::scopeLevelFor(wanted);
    const std::uint32_t perPoint = rt::scopeFramesPerPoint(level);
    const auto points =
        std::min<std::size_t>(static_cast<std::size_t>(std::ceil(wanted / perPoint)) + 2, rt::kScopePoints - 1);
    m_frame.points.resize(points);
    const rt::ScopeRead found = rt::readScope(slot, level, m_frame.points);
    m_frame.points.resize(found.points);

    const bool otherSound = !m_seenSlot || found.generation != m_generation;
    const auto now = static_cast<qint64>(m_hub->nowMs());
    if (otherSound) {
        m_seenSlot = true;
        m_generation = found.generation;
        m_lastHead.fill(0);
        m_wraps.fill(0);
        ++m_frame.run;
        m_movedAt = now;
    }
    if (found.head < m_lastHead[level]) {
        ++m_wraps[level];
    }
    const bool moved = found.head != m_lastHead[level] || level != m_level;
    m_lastHead[level] = found.head;
    m_level = level;
    m_frame.framesPerPoint = perPoint;
    m_frame.sampleRate = found.sampleRate;
    m_frame.cycleMs = cycleMs;
    m_frame.newestFrame =
        (static_cast<double>(m_wraps[level]) * 4294967296.0 + static_cast<double>(found.head)) * perPoint;
    if (moved && found.points > 0) {
        ++m_frame.serial;
        m_movedAt = now;
    }
    const auto unmoved = std::max(kUnmovedMs, static_cast<qint64>(2.0 * cycleMs));
    setState(now - m_movedAt >= unmoved ? State::NoNews : found.points > 0 ? State::Live : State::Waiting);
    if (moved && found.points > 0) {
        Q_EMIT refreshed();
    }
}

void ScopeFeed::tick()
{
    const MeterHub &meters = *m_hub->m_meters;
    const auto now = static_cast<qint64>(m_hub->nowMs());
    if (!m_hold && (m_renewedAt < 0 || now - m_renewedAt >= core::TelemetrySampler::kScopeRenewal.count())) {
        m_renewedAt = now;
        if (m_hub->m_asker && !m_hub->m_sessionId.isEmpty()) {
            m_hub->m_asker(m_hub->m_sessionId, m_blockId);
        }
    }
    if (m_hold) {
        return;
    }

    const QLatin1StringView word = meters.watchOf(m_blockId);
    if (word != TelemetryWatch::kWatching) {
        setState(word == TelemetryWatch::kOverloaded   ? State::Overloaded
                     : word == TelemetryWatch::kNoNews ? State::NoNews
                                                       : State::Stopped);
        m_wantedSince = now;
        m_movedAt = now;
        return;
    }
    const BlockLevels *levels = meters.block(m_blockId);
    if (levels == nullptr) {
        setState(State::Stopped);
        return;
    }
    if (!levels->enabled) {
        setState(State::Off);
        m_wantedSince = now;
        m_movedAt = now;
        return;
    }
    if (levels->scopeSlot < 0 || m_hub->m_pool == nullptr
        || static_cast<std::size_t>(levels->scopeSlot) >= rt::kScopeSlots) {
        setState(now - m_wantedSince >= kWaitForSlotMs ? State::NoRoom : State::Waiting);
        return;
    }
    if (levels->scopeSlot != m_slot) {
        m_slot = levels->scopeSlot;
        m_seenSlot = false;
    }
    const std::uint32_t rate = meters.sampleRate();
    const double cycleMs = rate > 0 ? 1000.0 * meters.frames() / rate : 0.0;
    read(m_hub->m_pool->slots[static_cast<std::size_t>(levels->scopeSlot)], cycleMs, rate);
}

ScopeHub::ScopeHub(const MeterHub *meters, QObject *parent)
    : QObject(parent)
    , m_meters(meters)
    , m_clock(&sinceTheHubStarted)
{
}

ScopeHub::~ScopeHub() = default;

void ScopeHub::watch(const QString &sessionId, const rt::ScopePool *pool)
{
    const bool same = sessionId == m_sessionId && pool == m_pool;
    m_sessionId = sessionId;
    m_pool = pool;
    if (same) {
        return;
    }
    for (auto &[blockId, subscribed] : m_feeds) {
        subscribed.feed->forget();
    }
}

ScopeFeed *ScopeHub::subscribe(const QString &blockId)
{
    Subscribed &subscribed = m_feeds[blockId];
    if (!subscribed.feed) {
        subscribed.feed = std::make_unique<ScopeFeed>(this, blockId);
        subscribed.feed->start();
        // At once, so that a surface opens saying the right thing.
        subscribed.feed->tick();
    }
    ++subscribed.count;
    return subscribed.feed.get();
}

void ScopeHub::unsubscribe(ScopeFeed *feed)
{
    if (feed == nullptr) {
        return;
    }
    const auto found = m_feeds.find(feed->blockId());
    if (found != m_feeds.end() && found->second.feed.get() == feed && --found->second.count <= 0) {
        m_feeds.erase(found);
    }
}

void ScopeHub::setAsker(Asker asker)
{
    m_asker = std::move(asker);
}

void ScopeHub::setClock(std::function<double()> clock)
{
    m_clock = clock ? std::move(clock) : std::function<double()>(&sinceTheHubStarted);
}

double ScopeHub::nowMs() const
{
    return m_clock();
}

} // namespace rb::ui

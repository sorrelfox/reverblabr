// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scopeview.h"

#include <algorithm>
#include <cmath>

namespace rb::ui
{

namespace
{

/// How far behind the newest sound a smooth view stays, beyond a refresh and
/// a cycle: two frames of a slow screen, for a refresh that comes late.
constexpr double kMarginMs = 33.0;
/// How far the clock may be out before it stops steering and goes there.
constexpr double kFarOffMs = 500.0;

[[nodiscard]] double refreshMs(const ScopeSettings &settings) noexcept
{
    return 1000.0 / std::max(settings.refreshHz, 1.0);
}

[[nodiscard]] double lagMs(const ScopeSettings &settings, double cycleMs) noexcept
{
    return refreshMs(settings) + cycleMs + kMarginMs;
}

/// Whether a short view eases rather than scrolls.
[[nodiscard]] bool settles(const ScopeSettings &settings) noexcept
{
    return settings.smooth && (settings.trigger || settings.spanMs < ScopeView::kScrollFromMs);
}

[[nodiscard]] bool scrolls(const ScopeSettings &settings) noexcept
{
    return settings.smooth && !settles(settings);
}

/// Whether a picture starts on a crossing: asked for, or to settle on.
[[nodiscard]] bool seeksCrossing(const ScopeSettings &settings) noexcept
{
    return settings.trigger || settles(settings);
}

/// A sample's height, with what can't be drawn pinned to the edge it is
/// nearer: a block that has blown up should look it.
[[nodiscard]] float heightOf(float value, float nonsense, const ScopeSettings &settings, double gain) noexcept
{
    if (!std::isfinite(value)) {
        return value < 0.0f ? -1.0f : value > 0.0f ? 1.0f : nonsense;
    }
    if (!settings.decibels) {
        return std::clamp(static_cast<float>(static_cast<double>(value) * gain), -1.0f, 1.0f);
    }
    const double level = std::fabs(static_cast<double>(value)) * gain;
    if (level <= 0.0) {
        return 0.0f;
    }
    const double above = 1.0 - 20.0 * std::log10(level) / ScopeView::kDecibelFloor;
    const auto height = static_cast<float>(std::clamp(above, 0.0, 1.0));
    return value < 0.0f ? -height : height;
}

} // namespace

double ScopeClock::advance(double newestFrame, double sampleRate, double lagFrames, double nowMs) noexcept
{
    const double wanted = newestFrame - lagFrames;
    const double farOff = sampleRate * kFarOffMs * 1.0e-3;
    if (!m_running || std::fabs(wanted - m_position) > std::max(farOff, lagFrames)) {
        m_running = true;
        m_position = wanted;
        m_lastMs = nowMs;
        return m_position;
    }
    const double dtMs = std::max(nowMs - m_lastMs, 0.0);
    m_lastMs = nowMs;
    m_position += sampleRate * dtMs * 1.0e-3;
    m_position += (wanted - m_position) * (1.0 - std::exp(-dtMs / kSteerMs));
    // Never past what has been heard: a late refresh waits, it does not make
    // anything up.
    m_position = std::min(m_position, newestFrame);
    return m_position;
}

double ScopeView::framesWanted(const ScopeSettings &settings, double sampleRate, double cycleMs)
{
    if (settings.everySample) {
        return static_cast<double>(rt::kScopePoints - 1);
    }
    double ms = settings.spanMs;
    if (seeksCrossing(settings)) {
        ms += std::max(settings.spanMs * 0.5, kLookBackMs);
    }
    if (scrolls(settings)) {
        // Twice: the clock runs evenly through news that comes in lumps, so
        // just after a lump it is that much further behind than the lag says.
        ms += 2.0 * lagMs(settings, cycleMs);
    }
    return ms * 1.0e-3 * sampleRate;
}

std::size_t ScopeView::traceCount(ScopeSettings::Channels channels) noexcept
{
    return channels == ScopeSettings::Channels::Both || channels == ScopeSettings::Channels::Stacked ? 2 : 1;
}

std::size_t ScopeView::traceOf(ScopeSettings::Channels channels, std::size_t index) noexcept
{
    switch (channels) {
    case ScopeSettings::Channels::Left:
        return rt::ScopeLeft;
    case ScopeSettings::Channels::Right:
        return rt::ScopeRight;
    case ScopeSettings::Channels::Mid:
        return rt::ScopeMid;
    case ScopeSettings::Channels::Side:
        return rt::ScopeSide;
    case ScopeSettings::Channels::Both:
    case ScopeSettings::Channels::Stacked:
        break;
    }
    return index == 0 ? rt::ScopeLeft : rt::ScopeRight;
}

void ScopeView::clear() noexcept
{
    m_started = false;
    m_clock.restart();
    for (std::size_t trace = 0; trace < kMaxTraces; ++trace) {
        m_shown[trace].clear();
        m_target[trace].clear();
    }
    m_columns = 0;
    m_seenSerial = 0;
}

double ScopeView::locate(const ScopeFrame &frame, const ScopeSettings &settings, double spanPoints, double nowMs)
{
    const auto count = static_cast<double>(frame.points.size());
    const double perPoint = frame.framesPerPoint;
    const double firstFrame = frame.newestFrame - count * perPoint;
    m_crossed = false;

    if (seeksCrossing(settings) && !frame.points.empty()) {
        // Far enough back that a whole view fits after it.
        const double latest = count - 1.0 - spanPoints;
        if (latest >= 1.0) {
            const dsp::ScopeCrossing crossing = dsp::latestRisingCrossing(
                frame.points, traceOf(settings.channels, 0), static_cast<std::size_t>(latest));
            if (crossing.found) {
                m_crossed = true;
                m_shift = 0.0;
                m_firstColumn = 0;
                m_endFrame = firstFrame + (crossing.point + spanPoints) * perPoint;
                return crossing.point;
            }
        }
    }

    double end = frame.newestFrame;
    if (scrolls(settings)) {
        const double lag = lagMs(settings, frame.cycleMs) * 1.0e-3 * frame.sampleRate;
        end = m_clock.advance(frame.newestFrame, frame.sampleRate, lag, nowMs);
    }
    m_endFrame = end;
    // The last point stands for the time up to \a end, and is where the view
    // ends.
    return (end - firstFrame) / perPoint - 1.0 - spanPoints;
}

void ScopeView::fit(const ScopeSettings &settings, double dtMs)
{
    if (!settings.autoFit) {
        m_gainDb = settings.gainDb;
        return;
    }
    float loudest = 0.0f;
    for (std::size_t trace = 0; trace < m_traces; ++trace) {
        for (const dsp::ScopeColumn &column : m_raw[trace]) {
            if (column.heard) {
                const float magnitude = std::max(std::fabs(column.low), std::fabs(column.high));
                if (std::isfinite(magnitude)) {
                    loudest = std::max(loudest, magnitude);
                }
            }
        }
    }
    constexpr double kQuietestDb = -80.0;
    constexpr double kHighestFitDb = 48.0;
    const double loudestDb = 20.0 * std::log10(std::max(static_cast<double>(loudest), 1.0e-9));
    if (loudestDb < kQuietestDb) {
        // Not enlarging a noise floor to the top of the view.
        return;
    }
    const double wanted = std::min(20.0 * std::log10(kFitTo) - loudestDb, kHighestFitDb);
    if (wanted <= m_gainDb || !m_started) {
        m_gainDb = wanted; // Up to fit at once: nothing is to go off the edge.
    } else {
        m_gainDb += (wanted - m_gainDb) * (1.0 - std::exp(-dtMs / kFitLetGoMs));
    }
}

void ScopeView::scale(const ScopeSettings &settings)
{
    const double gain = std::pow(10.0, m_gainDb / 20.0);
    for (std::size_t trace = 0; trace < m_traces; ++trace) {
        m_target[trace].resize(m_raw[trace].size());
        for (std::size_t column = 0; column < m_raw[trace].size(); ++column) {
            const dsp::ScopeColumn &from = m_raw[trace][column];
            dsp::ScopeColumn &into = m_target[trace][column];
            into.heard = from.heard;
            into.low = from.heard ? heightOf(from.low, -1.0f, settings, gain) : 0.0f;
            into.high = from.heard ? heightOf(from.high, 1.0f, settings, gain) : 0.0f;
        }
    }
}

bool ScopeView::update(const ScopeFrame &frame, const ScopeSettings &settings, std::size_t columns, double nowMs)
{
    if (frame.run != m_seenRun) {
        clear();
        m_seenRun = frame.run;
    }
    const bool asked = settings.channels != m_seen.channels || settings.spanMs != m_seen.spanMs
        || settings.gainDb != m_seen.gainDb || settings.autoFit != m_seen.autoFit || settings.trigger != m_seen.trigger
        || settings.decibels != m_seen.decibels || settings.smooth != m_seen.smooth;
    const bool news = frame.serial != m_seenSerial;
    if (columns == 0 || frame.points.empty() || frame.sampleRate == 0) {
        return false;
    }
    const bool resized = columns != m_columns;
    if (!settings.smooth && !news && !asked && !resized) {
        return false;
    }
    if (asked) {
        // Another stretch or shape: easing from the last one means nothing.
        m_clock.restart();
    }
    const double dtMs = m_started ? std::max(nowMs - m_lastMs, 0.0) : 0.0;
    const bool ease = settles(settings) && m_started && !asked && !resized && settings.smooth;
    m_seen = settings;
    m_seenSerial = frame.serial;
    m_lastMs = nowMs;
    m_columns = columns;
    m_traces = traceCount(settings.channels);

    const double pointsPerMs = frame.sampleRate * 1.0e-3 / frame.framesPerPoint;
    const double spanPoints = settings.spanMs * pointsPerMs;
    const double pointsPerColumn = spanPoints / static_cast<double>(columns);

    double first = locate(frame, settings, spanPoints, nowMs);
    std::size_t count = columns;
    if (!m_crossed) {
        // Back to the nearest column edge counted from the start of the sound.
        const double perPoint = frame.framesPerPoint;
        const double firstFrame = frame.newestFrame - static_cast<double>(frame.points.size()) * perPoint;
        const double fromStart = (firstFrame / perPoint + first) / pointsPerColumn;
        const double whole = std::floor(fromStart);
        m_shift = fromStart - whole;
        m_firstColumn = whole > 0.0 ? static_cast<std::uint64_t>(whole) : 0U;
        first -= m_shift * pointsPerColumn;
        // And one more, for the piece of a column the shift uncovers.
        count = columns + 1;
    }

    for (std::size_t trace = 0; trace < m_traces; ++trace) {
        m_raw[trace].resize(count);
        dsp::scopeColumns(frame.points, traceOf(settings.channels, trace), first, pointsPerColumn, m_raw[trace]);
    }
    fit(settings, dtMs);
    scale(settings);

    for (std::size_t trace = 0; trace < m_traces; ++trace) {
        std::vector<dsp::ScopeColumn> &shown = m_shown[trace];
        const std::vector<dsp::ScopeColumn> &target = m_target[trace];
        if (!ease || shown.size() != target.size()) {
            shown = target;
            continue;
        }
        const double settle = std::max(refreshMs(settings), kLeastSettleMs);
        const auto share = static_cast<float>(1.0 - std::exp(-dtMs / settle));
        for (std::size_t column = 0; column < shown.size(); ++column) {
            if (!shown[column].heard || !target[column].heard) {
                shown[column] = target[column];
                continue;
            }
            shown[column].low += (target[column].low - shown[column].low) * share;
            shown[column].high += (target[column].high - shown[column].high) * share;
        }
    }
    m_started = true;
    return true;
}

} // namespace rb::ui

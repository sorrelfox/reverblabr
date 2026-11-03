// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "spectrumview.h"

#include <algorithm>
#include <cmath>

namespace rb::ui
{

void SpectrumAnalysis::update(const ScopeFrame &frame, std::size_t frameSize, std::size_t trace)
{
    if (frame.run != m_seenRun) {
        m_seenRun = frame.run;
        m_ready = false;
        m_analysedTo = 0.0;
        ++m_run;
    }
    const bool asked = frameSize != m_spectrum.frameSize() || trace != m_trace;
    if ((frame.serial == m_seenSerial && !asked) || frame.framesPerPoint != 1 || frame.sampleRate == 0) {
        return;
    }
    if (frameSize != m_spectrum.frameSize()) {
        m_spectrum.prepare(frameSize);
    }
    if (asked) {
        // A look at the sound there is, not at what went by since the last.
        m_ready = false;
        m_trace = trace;
    }
    const std::size_t needed = m_spectrum.samplesNeeded();
    if (frame.points.size() < needed) {
        // Not there yet, or the writer overtook the copy, as it will once in
        // a long while at the largest frame. The next look covers this one.
        m_seenSerial = frame.serial;
        return;
    }
    const std::size_t hop = m_spectrum.hop();
    const std::size_t reachable = (frame.points.size() - needed) / hop + 1;
    const double fresh = m_ready ? frame.newestFrame - m_analysedTo : 0.0;
    const auto frames =
        std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(fresh / static_cast<double>(hop))), 1, reachable);

    m_samples.resize(frame.points.size());
    std::ranges::transform(
        frame.points, m_samples.begin(), [trace](const rt::ScopePoint &point) { return point.low[trace]; });
    m_spectrum.clear();
    for (std::size_t look = 0; look < frames; ++look) {
        const std::size_t end = m_samples.size() - look * hop;
        m_spectrum.add(std::span<const float>(m_samples).subspan(end - needed, needed));
    }
    m_transforms += frames;
    m_seenSerial = frame.serial;
    m_analysedTo = frame.newestFrame;
    m_sampleRate = frame.sampleRate;
    m_ready = true;
    ++m_serial;
}

void SpectrumView::clear() noexcept
{
    m_shown.clear();
    m_target.clear();
    m_peaks.clear();
    m_peakSinceMs.clear();
    m_seenSerial = 0;
}

float SpectrumView::heightOf(float levelDb, double floorDb) noexcept
{
    const double above = (static_cast<double>(levelDb) - floorDb) / (kTopDb - floorDb);
    return static_cast<float>(std::clamp(above, 0.0, 1.0));
}

void SpectrumView::movePeaks(double dtMs, double nowMs)
{
    const auto fall = static_cast<float>(kPeakFallDbPerSecond * dtMs * 1.0e-3);
    for (std::size_t bar = 0; bar < m_shown.size(); ++bar) {
        if (m_shown[bar] >= m_peaks[bar]) {
            m_peaks[bar] = m_shown[bar];
            m_peakSinceMs[bar] = nowMs;
        } else if (nowMs - m_peakSinceMs[bar] >= kPeakHoldMs) {
            m_peaks[bar] = std::max(m_peaks[bar] - fall, m_shown[bar]);
        }
    }
}

bool SpectrumView::update(const SpectrumAnalysis &analysis, const SpectrumSettings &settings, double nowMs)
{
    if (analysis.run() != m_seenRun) {
        clear();
        m_seenRun = analysis.run();
    }
    if (!analysis.isReady() || settings.bars == 0) {
        return false;
    }
    const bool laidOut = settings.bars == m_map.bars() && settings.scale == m_seen.scale
        && settings.tiltDb == m_seen.tiltDb && analysis.transformSize() == m_seenTransform
        && analysis.sampleRate() == m_seenRate;
    const bool asked =
        !laidOut || settings.frameSize != m_seen.frameSize || settings.trace != m_seen.trace || m_shown.empty();
    const bool news = analysis.serial() != m_seenSerial;
    if (!settings.smooth && !news && !asked) {
        return false;
    }
    if (!laidOut || m_target.empty()) {
        m_map.prepare(settings.bars, settings.scale, analysis.sampleRate(), analysis.transformSize(), settings.tiltDb);
        m_seenTransform = analysis.transformSize();
        m_seenRate = analysis.sampleRate();
        m_target.resize(settings.bars);
    }
    const double dtMs = m_shown.empty() ? 0.0 : std::max(nowMs - m_lastMs, 0.0);
    m_seen = settings;
    m_seenSerial = analysis.serial();
    m_lastMs = nowMs;

    if (news || asked) {
        m_map.read(analysis.power(), m_target);
        for (float &level : m_target) {
            level = std::clamp(level, kLowestDb, kHighestDb);
        }
    }
    if (asked) {
        // Another shape: easing from the last one means nothing, and neither
        // does a mark left from it.
        m_shown = m_target;
        m_peaks = m_target;
        m_peakSinceMs.assign(m_target.size(), nowMs);
        return true;
    }
    if (settings.smooth) {
        const double refreshMs = 1000.0 / std::max(settings.refreshHz, 1.0);
        const auto rise = static_cast<float>(1.0 - std::exp(-dtMs / std::max(kRiseMs, kRiseRefreshes * refreshMs)));
        const auto fall = static_cast<float>(1.0 - std::exp(-dtMs / std::max(kFallMs, kFallRefreshes * refreshMs)));
        for (std::size_t bar = 0; bar < m_shown.size(); ++bar) {
            const float towards = m_target[bar] - m_shown[bar];
            m_shown[bar] += towards * (towards > 0.0f ? rise : fall);
        }
    } else {
        m_shown = m_target;
    }
    movePeaks(dtMs, nowMs);
    return true;
}

} // namespace rb::ui

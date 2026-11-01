// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/transientcut.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::dsp
{

namespace
{

/// Keeps a mean of nothing from being a divide by zero.
constexpr float kLeastPower = 1.0e-24f;

[[nodiscard]] std::uint32_t framesOf(double ms, double sampleRate, std::uint32_t hop)
{
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(ms * 1.0e-3 * sampleRate / hop)));
}

} // namespace

void TransientCut::prepare(double sampleRate)
{
    m_frame = std::max<std::uint32_t>(4, static_cast<std::uint32_t>(std::lround(kFrameMs * 1.0e-3 * sampleRate)));
    m_hop = std::max<std::uint32_t>(2, static_cast<std::uint32_t>(std::lround(kHopMs * 1.0e-3 * sampleRate)));
    // The two tapers have to come out equal.
    m_hop += (m_frame - m_hop) % 2;
    m_taper = (m_frame - m_hop) / 2;

    // A third again of the frame, as 48 samples in 64 and 44 in 64.
    std::uint32_t size = 4;
    while (size * 3 < m_frame * 4) {
        size *= 2;
    }
    m_fft.prepare(size);

    m_pastFrames = framesOf(kPastMs, sampleRate, m_hop);
    m_holdFrames = framesOf(kHoldMs, sampleRate, m_hop);
    m_learnFrames = framesOf(kLearnMs, sampleRate, m_hop);
    m_overEndsFrames = framesOf(kOverEndsMs, sampleRate, m_hop);
    m_letGoStepDb = kMostCutDb / static_cast<float>(framesOf(kLetGoMs, sampleRate, m_hop));

    m_quietPower = std::pow(10.0, double{kQuietDb} / 10.0);

    m_window.assign(m_frame, 1.0f);
    for (std::uint32_t n = 0; n < m_taper; ++n) {
        const double phase = std::numbers::pi * (n + 0.5) / m_taper;
        const auto value = static_cast<float>(0.5 - 0.5 * std::cos(phase));
        m_window[n] = value;
        m_window[m_frame - 1 - n] = value;
    }

    m_ring.assign(m_frame, 0.0f);
    m_ordered.assign(m_frame, 0.0f);
    m_padded.assign(size, 0.0f);
    m_out.assign(m_hop, 0.0f);
    m_spectrum.assign(m_fft.bins(), {});
    m_history.assign(kRanges * std::size_t{m_pastFrames}, 0.0f);

    const double binHz = sampleRate / size;
    const auto bins = static_cast<std::uint32_t>(m_fft.bins());
    std::uint32_t first = 0;
    for (std::size_t range = 0; range < kRanges; ++range) {
        std::uint32_t end = first;
        while (end < bins && end * binHz < kRangeEndsHz[range]) {
            ++end;
        }
        m_bins[range] = {first, end};
        m_past[range].offset = range * m_pastFrames;
        first = end;
    }
    m_share = 0.0f;
    reset();
}

void TransientCut::reset() noexcept RB_NONBLOCKING
{
    std::ranges::fill(m_ring, 0.0f);
    std::ranges::fill(m_out, 0.0f);
    m_ringAt = 0;
    m_outAt = 0;
    for (Past &past : m_past) {
        past.count = 0;
        past.at = 0;
        past.sum = 0.0;
        past.fresh = 0.0;
    }
    m_cut.fill(0.0f);
    m_over.fill(0);
    m_under.fill(0);
    m_heldCut.fill(0.0f);
    m_lettingGo.fill(false);
}

float TransientCut::deepestCutDb() const noexcept RB_NONBLOCKING
{
    return std::ranges::max(m_cut);
}

void TransientCut::push(std::size_t range, float power) noexcept RB_NONBLOCKING
{
    Past &past = m_past[range];
    float &slot = m_history[past.offset + past.at];
    if (past.count == m_pastFrames) {
        past.sum -= slot;
    } else {
        ++past.count;
    }
    slot = power;
    past.sum += power;
    past.fresh += power;
    if (++past.at == m_pastFrames) {
        past.at = 0;
        past.sum = past.fresh;
        past.fresh = 0.0;
    }
}

void TransientCut::runFrame() noexcept RB_NONBLOCKING
{
    // Oldest first: the ring's write position is the frame's start.
    const auto split = static_cast<std::ptrdiff_t>(m_ringAt);
    std::copy(m_ring.begin() + split, m_ring.end(), m_ordered.begin());
    std::copy(m_ring.begin(), m_ring.begin() + split, m_ordered.end() - split);

    double energy = 0.0;
    for (std::uint32_t n = 0; n < m_frame; ++n) {
        const float sample = m_ordered[n];
        energy += double{sample} * sample;
        m_padded[n] = sample * m_window[n];
    }
    std::fill(m_padded.begin() + m_frame, m_padded.end(), 0.0f);
    const bool quiet = energy / m_frame < m_quietPower;
    m_fft.forward(m_padded, m_spectrum);

    bool cutAny = false;
    for (std::size_t range = 0; range < kRanges; ++range) {
        const Bins bins = m_bins[range];
        if (bins.first == bins.end) {
            continue; // A rate too low to reach this range.
        }
        float power = 0.0f;
        for (std::uint32_t bin = bins.first; bin < bins.end; ++bin) {
            power += std::norm(m_spectrum[bin]);
        }
        power /= static_cast<float>(bins.end - bins.first);

        const Past &past = m_past[range];
        const float mean = std::max(kLeastPower, static_cast<float>(past.sum / std::max<std::uint32_t>(1, past.count)));
        const float threshold = kThresholds[range];
        const bool over =
            m_share > 0.0f && past.count >= m_learnFrames && !quiet && power > threshold * threshold * mean;
        float last = m_cut[range];
        // **The hold is time since the rise.** A narrow range dips under its
        // threshold all through a level that stays, and a sound that stays can
        // have gaps of its own, so the count runs on through a dip. Only
        // kOverEndsMs with nothing over starts it again.
        if (over) {
            ++m_over[range];
            m_under[range] = 0;
        } else if (m_over[range] > 0) {
            ++m_over[range];
            // A hold that has run out stays spent while the past learns the
            // new level, or the last chance peak over it would be cut anew.
            if (++m_under[range] >= (m_over[range] > m_holdFrames ? m_pastFrames : m_overEndsFrames)) {
                m_over[range] = 0;
                m_under[range] = 0;
            }
        }
        // A level that stays, or a share taken away: no step back up to it.
        // The hold can end on a dip, so this starts from the cut in place
        // before it.
        if (m_over[range] == m_holdFrames + 1 || (m_share <= 0.0f && m_heldCut[range] > 0.0f)) {
            m_lettingGo[range] = true;
            last = m_heldCut[range];
            m_heldCut[range] = 0.0f;
        }
        const bool holding = over && m_over[range] <= m_holdFrames;
        float cut = 0.0f;
        if (m_lettingGo[range]) {
            cut = std::max(0.0f, last - m_letGoStepDb);
            m_lettingGo[range] = cut > 0.0f;
        } else if (holding) {
            cut = std::min(m_share * 10.0f * std::log10(power / mean), kMostCutDb);
            m_heldCut[range] = cut;
        }
        if (m_over[range] == 0) {
            m_heldCut[range] = 0.0f;
        }
        // What is being softened stays out of the past, and so does silence.
        if (!quiet && !holding) {
            push(range, power);
        }
        m_cut[range] = cut;
        cutAny = cutAny || cut > 0.0f;
    }

    if (!cutAny) {
        std::copy_n(m_ordered.begin() + m_taper, m_hop, m_out.begin());
        return;
    }
    // Bins past the last range follow it.
    const auto bins = static_cast<std::uint32_t>(m_spectrum.size());
    std::array<Bins, kRanges> reach = m_bins;
    reach.back().end = bins;
    for (std::size_t range = 0; range < kRanges; ++range) {
        if (m_cut[range] > 0.0f) {
            const float gain = dbToGain(-m_cut[range]);
            for (std::uint32_t bin = reach[range].first; bin < reach[range].end; ++bin) {
                m_spectrum[bin] *= gain;
            }
        }
    }
    m_fft.inverse(m_spectrum, m_padded);
    std::copy_n(m_padded.begin() + m_taper, m_hop, m_out.begin());
}

} // namespace rb::dsp

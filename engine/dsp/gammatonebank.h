// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::dsp
{

/// The filterbank of the listening blocks (ADR-0019): one ear split into
/// channels on the ERB scale, a gain on each, and the channels put back
/// together flat and 4 ms late.
///
/// **Each channel is a fourth-order complex gammatone filter** (Hohmann
/// 2002): four complex one-pole sections in a row, one ERB wide. Its output
/// is the channel's analytic signal, so |y|² is its power with no rectifier
/// to smooth. To put the channels back together, each one's real part is
/// delayed so that its impulse response peaks at one common delay, turned so
/// it peaks positive there, weighted so the sum is flat, and added.
///
/// **The gains change on a fixed grid of kControlFrames frames**, whatever
/// the length of a cycle, so the same sound in cycles of any size comes out
/// the same. At the end of each stretch of the grid the bank hands the
/// caller each channel's mean power over it and takes the gains for the
/// next; the new gains are ramped in across that next stretch. The gain is
/// applied where the channel leaves its delay line, so for every channel but
/// the lowest the gain for a sound is decided before the sound goes out.
///
/// Everything is made in prepare(); process() allocates nothing and does
/// not block.
class GammatoneBank
{
public:
    /// How many frames apart the gains may change: two thirds of a
    /// millisecond at 48 kHz, finer than the fastest attack a block offers.
    static constexpr std::size_t kControlFrames = 32;
    /// The delay the channels are lined up to, which a block reports.
    static constexpr double kDefaultDelayMs = 4.0;
    /// The channels both Hearing Compensation blocks run.
    static constexpr std::size_t kDefaultChannels = 64;

    /// Designs \a channels channels evenly spaced on the ERB scale from 100 Hz
    /// to 16 kHz for \a sampleRate, aligned at \a delayMs. Allocates.
    void prepare(double sampleRate, std::size_t channels = kDefaultChannels, double delayMs = kDefaultDelayMs);

    /// The channels' centres, in hertz, low to high.
    [[nodiscard]] std::span<const double> centresHz() const noexcept
    {
        return m_centres;
    }

    [[nodiscard]] std::size_t channelCount() const noexcept RB_NONBLOCKING
    {
        return m_count;
    }

    /// The common delay, in samples: what a block built on the bank reports.
    [[nodiscard]] std::uint32_t latencySamples() const noexcept RB_NONBLOCKING
    {
        return m_delay;
    }

    /// What one channel's power reads for a sine of amplitude 1 at \a hz:
    /// 1 at the channel's centre, less away from it. For measuring levels in
    /// a layout of bands over the channels, and for tests.
    [[nodiscard]] double sinePower(std::size_t channel, double hz) const;

    /// Back to silence, with every gain at 1. Real-time.
    void reset() noexcept RB_NONBLOCKING;

    /// **Filters \a samples in place**, calling \a control(power, gains) at
    /// the end of every stretch of kControlFrames frames: \a power is each
    /// channel's mean power over it (a sine of amplitude 1 at a channel's
    /// centre reads 1 there), and \a gains is where the caller writes each
    /// channel's linear gain for the next stretch. The gains written stay
    /// until they are written again.
    template <typename Control>
    void process(std::span<float> samples, Control &&control) noexcept RB_NONBLOCKING
    {
        std::size_t done = 0;
        while (done < samples.size()) {
            const std::size_t frames = std::min(samples.size() - done, kControlFrames - m_position);
            processRun(samples.subspan(done, frames));
            done += frames;
            if (m_position == kControlFrames) {
                finishStretch();
                control(std::span<const float>{m_power}, std::span<float>{m_target});
                startStretch();
            }
        }
    }

private:
    /// Filters \a samples, all inside the current stretch of the grid.
    void processRun(std::span<float> samples) noexcept RB_NONBLOCKING;
    /// Turns the power gathered over a whole stretch into its mean.
    void finishStretch() noexcept RB_NONBLOCKING;
    /// Sets each channel's ramp from the gain it has to its new target.
    void startStretch() noexcept RB_NONBLOCKING;

    std::size_t m_count = 0;
    std::uint32_t m_delay = 0;
    std::size_t m_position = 0;
    std::vector<double> m_centres;

    // One array per quantity, a value a channel: the layout that lets a
    // later version run four or eight channels at once.
    std::vector<float> m_poleRe;
    std::vector<float> m_poleIm;
    std::vector<float> m_inputGain;
    std::vector<float> m_phaseRe;
    std::vector<float> m_phaseIm;
    std::vector<float> m_weight;
    /// The four sections' states: stage s of channel c at [s * count + c].
    std::vector<float> m_stateRe;
    std::vector<float> m_stateIm;

    /// Every channel's delay line in one buffer: channel c's starts at
    /// m_lineStart[c] and is m_lineLength[c] long.
    std::vector<float> m_lines;
    std::vector<std::size_t> m_lineStart;
    std::vector<std::size_t> m_lineLength;
    std::vector<std::size_t> m_lineWrite;

    std::vector<float> m_gain; ///< Applied at the start of this stretch.
    std::vector<float> m_step; ///< Added a frame across this stretch.
    std::vector<float> m_target; ///< Where this stretch's ramp ends; the caller's.
    std::vector<float> m_powerSum;
    std::vector<float> m_power;
    std::vector<float> m_sum; ///< One stretch's resynthesis.

    // What sinePower() needs, kept from the design.
    double m_sampleRate = 48000.0;
    std::vector<double> m_poleRadius;
    std::vector<double> m_poleAngle;
};

} // namespace rb::dsp

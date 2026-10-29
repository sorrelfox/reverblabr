// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audioblock.h"
#include "dsp/decibel.h"
#include "dsp/smoothing.h"
#include "dsp/truepeak.h"
#include "rt/realtime.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rb::dsp
{

/// A sample-peak limiter that knows what is coming.
///
/// **It looks ahead, so it never lets a peak past.** The signal is delayed
/// by the lookahead, and the gain applied to it is the smallest the next
/// lookahead of samples will need. A limiter that only reacted to what it
/// had already seen would either let the first sample of a transient
/// through or would have to distort the ones before it.
///
/// **It is always in the signal path**, which is what makes the latency a
/// constant a block can report. Below the ceiling it applies a gain of
/// exactly one, so the output is the input delayed and nothing else — a
/// null test against the delayed input is how that is checked, not a
/// listening session.
///
/// **The gain is linked across channels**, so a peak on one side takes both
/// down together and the stereo image does not move.
///
/// The gain the window needs falls at once and rises at the release rate,
/// and what is applied is that averaged over the lookahead: so the gain
/// comes down as a ramp that ends where it has to be just as the loud
/// sample arrives, and nothing is clipped on the way. A step would bend the
/// waveform, and ring above the ceiling between the samples.
///
/// **Sample peak or true peak.** Prepared true-peak capable, it can detect
/// with TruePeakEstimator instead, four times oversampled, and holds the
/// signal back that estimate's TruePeakEstimator::kDelay samples longer in
/// either mode.
class LookaheadLimiter
{
public:
    /// The ceiling every M1 limiter uses: a sample below full scale, which
    /// is what keeps a converter or an encoder out of trouble.
    static constexpr float kDefaultCeilingDb = -1.0f;
    /// How far ahead it looks. 1.5 ms is long enough to catch a transient
    /// and short enough that the delay is not something a person notices.
    static constexpr float kDefaultLookaheadMs = 1.5f;
    /// How fast the gain comes back up when the loud part has passed.
    static constexpr float kDefaultReleaseMs = 80.0f;
    /// How long a new ceiling takes to arrive: a ceiling put in place at
    /// once would take the gain down in one step, which is a click.
    static constexpr float kCeilingGlideMs = 20.0f;
    /// How far under the ceiling a true peak is aimed. **What the estimate
    /// and a finer meter disagree by**: four points a sample against a
    /// 128-tap reference differ by a few hundredths of a decibel on
    /// band-limited programme, and the ceiling is a promise.
    static constexpr float kTruePeakMarginDb = 0.05f;

    /// Makes the buffers. **Not for the audio thread**: everything after
    /// this is bounded and allocates nothing.
    ///
    /// **\a truePeakCapable makes room for setTruePeak()**: the signal is
    /// held back TruePeakEstimator::kDelay samples longer, so the latency is
    /// the same whichever way it is detecting and switching does not move
    /// the lane. Volume and Auto Level leave it off and keep the shorter
    /// delay.
    void prepare(double sampleRate,
                 std::uint32_t channels,
                 float ceilingDb = kDefaultCeilingDb,
                 float lookaheadMs = kDefaultLookaheadMs,
                 float releaseMs = kDefaultReleaseMs,
                 bool truePeakCapable = false)
    {
        m_channels = channels;
        m_sampleRate = sampleRate;
        m_ceiling = dbToGain(ceilingDb);
        m_ceilingGlide.prepare(sampleRate, kCeilingGlideMs);
        m_lookahead = static_cast<std::uint32_t>(std::lround(static_cast<double>(lookaheadMs) * 1.0e-3 * sampleRate));
        m_lookahead = std::max(m_lookahead, 1U);
        // **The window is one longer than the delay.** The gain put on the
        // sample leaving the delay line has to suit that sample and every
        // one still inside it, which is lookahead + 1 samples in all.
        m_window = m_lookahead + 1;
        m_truePeakCapable = truePeakCapable;
        m_detectionDelay = truePeakCapable ? TruePeakEstimator::kDelay : 0;
        m_delayLength = m_lookahead + m_detectionDelay;
        if (truePeakCapable) {
            m_estimator.prepare(channels);
        }

        m_delay.assign(static_cast<std::size_t>(channels) * m_delayLength, 0.0f);
        m_required.assign(m_window, 1.0f);
        m_wedge.assign(m_window, 0);
        m_smoothing.assign(m_lookahead, 1.0f);

        setReleaseMs(releaseMs);
        reset();
    }

    /// Moves the ceiling, as a gain, over kCeilingGlideMs. Real-time. **A
    /// step here would be a step in the gain**, so it glides; the same
    /// ceiling again does nothing, so a block may set it every cycle.
    void setCeilingGain(float ceiling) noexcept RB_NONBLOCKING
    {
        m_ceiling = ceiling;
        m_ceilingGlide.setTarget(ceiling);
    }

    /// How long the gain takes to come back from silence to unity. Real-time.
    void setReleaseMs(float releaseMs) noexcept RB_NONBLOCKING
    {
        // The gap to the gain the window allows closes by this factor a
        // sample: two thirds of the way in the release time.
        const double samples = std::max(1.0, static_cast<double>(releaseMs) * 1.0e-3 * m_sampleRate);
        m_releaseCoefficient = std::exp(-1.0 / samples);
    }

    /// Detects true peaks, four times oversampled, rather than sample
    /// peaks. Only a limiter prepared true-peak capable can; for any other
    /// it does nothing. Real-time.
    void setTruePeak(bool on) noexcept RB_NONBLOCKING
    {
        m_truePeak = on && m_truePeakCapable;
    }

    [[nodiscard]] bool truePeak() const noexcept RB_NONBLOCKING
    {
        return m_truePeak;
    }

    /// How many samples the signal is delayed by, which is what a block
    /// reports as its latency.
    [[nodiscard]] std::uint32_t latencySamples() const noexcept RB_NONBLOCKING
    {
        return m_delayLength;
    }

    [[nodiscard]] float ceiling() const noexcept RB_NONBLOCKING
    {
        return m_ceiling;
    }

    /// Forgets the signal, keeping the shape. Real-time: it writes zeros.
    void reset() noexcept RB_NONBLOCKING
    {
        std::ranges::fill(m_delay, 0.0f);
        std::ranges::fill(m_required, 1.0f);
        m_ceilingGlide.reset(m_ceiling);
        std::ranges::fill(m_smoothing, 1.0f);
        m_smoothIndex = 0;
        m_smoothSum = static_cast<double>(m_smoothing.size());
        m_held = 1.0;
        m_lowestGain = 1.0f;
        if (m_truePeakCapable) {
            m_estimator.reset();
        }
        m_write = 0;
        m_position = 0;
        m_wedgeFront = 0;
        m_wedgeCount = 0;
        m_gain = 1.0f;
    }

    /// Limits one cycle in place. Real-time: bounded work, no allocation.
    void process(const AudioBlock &block, std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        if (m_window == 0) {
            return;
        }
        const std::uint32_t channels = std::min(m_channels, block.channelCount());
        m_lowestGain = m_gain;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const float ceiling = m_ceilingGlide.next();
            float loudest = 0.0f;
            if (m_truePeakCapable) {
                // The estimate is about the sample kDelay ago, which is why
                // the audio is held back that much longer: the requirement
                // and the sample it is for still meet at the far end.
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    const std::span<float> samples = block.channel(channel);
                    m_estimator.write(channel, frame < samples.size() ? samples[frame] : 0.0f);
                }
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    loudest = std::max(loudest,
                                       m_truePeak ? m_estimator.peak(channel) * kTruePeakMargin
                                                  : std::fabs(m_estimator.delayedSample(channel)));
                }
                m_estimator.advance();
            } else {
                for (std::uint32_t channel = 0; channel < channels; ++channel) {
                    const std::span<float> samples = block.channel(channel);
                    if (frame < samples.size()) {
                        loudest = std::max(loudest, std::fabs(samples[frame]));
                    }
                }
            }
            // **The gain is linked across channels**, so a peak on one side
            // takes both down together and the stereo image does not move.
            pushRequired(loudest > ceiling ? ceiling / loudest : 1.0f);

            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const std::span<float> samples = block.channel(channel);
                if (frame >= samples.size()) {
                    continue;
                }
                const std::size_t slot = static_cast<std::size_t>(channel) * m_delayLength + m_write;
                const float delayed = m_delay[slot];
                m_delay[slot] = samples[frame];
                samples[frame] = delayed;
            }

            // The smallest gain the window needs, in place before the sample
            // that needs it arrives. It falls at once and rises at the
            // release rate.
            const double target = m_required[m_wedge[m_wedgeFront] % m_window];
            // **In double**: near unity a float gap shrinking by a thousandth
            // a sample rounds back to itself and the gain stalls a hair
            // under one for ever.
            m_held = target < m_held ? target : target - (target - m_held) * m_releaseCoefficient;

            // **Then averaged over the lookahead**, so the gain comes
            // down as a ramp rather than a step. A step in the gain is a
            // step in the signal's slope: it bends a sine (0.35 % THD at
            // 200 Hz held 6 dB down) and it rings between the samples,
            // where a true peak lives. The average cannot let anything
            // through: every one of the last `lookahead` held gains was
            // already small enough for the sample leaving now, because each
            // window they came from had that sample in it.
            const auto held = static_cast<float>(m_held);
            m_smoothSum += static_cast<double>(held) - static_cast<double>(m_smoothing[m_smoothIndex]);
            m_smoothing[m_smoothIndex] = held;
            if (++m_smoothIndex == m_lookahead) {
                // Added up afresh once a lap, so the sum cannot drift over
                // hours of adding and taking away.
                m_smoothIndex = 0;
                m_smoothSum = 0.0;
                for (const float each : m_smoothing) {
                    m_smoothSum += static_cast<double>(each);
                }
            }
            m_gain = static_cast<float>(m_smoothSum / static_cast<double>(m_lookahead));
            m_lowestGain = std::min(m_lowestGain, m_gain);
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const std::span<float> samples = block.channel(channel);
                if (frame < samples.size()) {
                    samples[frame] *= m_gain;
                }
            }

            m_write = m_write + 1 == m_delayLength ? 0 : m_write + 1;
        }
    }

    /// The gain it is applying at the moment, for a meter or a test.
    [[nodiscard]] float gain() const noexcept RB_NONBLOCKING
    {
        return m_gain;
    }

    /// The smallest gain it applied in the last cycle: what a
    /// gain-reduction meter shows, so a short grab is not missed between
    /// two readings.
    [[nodiscard]] float lowestGain() const noexcept RB_NONBLOCKING
    {
        return m_lowestGain;
    }

private:
    /// kTruePeakMarginDb as the factor a true-peak estimate is scaled up by.
    static constexpr float kTruePeakMargin = 1.005773f;

    /// Keeps the positions of the window in increasing order of what they
    /// require, so that the front is always the smallest. Amortised O(1) a
    /// sample, and nothing allocated: the ring is as long as the window.
    void pushRequired(float required) noexcept RB_NONBLOCKING
    {
        const std::uint64_t position = m_position++;
        m_required[position % m_window] = required;

        // **Out of the window before anything goes in.** The ring is exactly
        // as long as the window, so a push before the expired entries have
        // gone would write over the front — and the front is the one that
        // matters, being the sample about to be written out.
        while (m_wedgeCount > 0 && at(0) + m_window <= position) {
            m_wedgeFront = m_wedgeFront + 1 == m_window ? 0 : m_wedgeFront + 1;
            --m_wedgeCount;
        }
        // **No smaller, not merely larger.** Popping equals as well keeps
        // the wedge strictly increasing, and with it the count at or below
        // the window however long the signal holds steady.
        while (m_wedgeCount > 0 && m_required[at(m_wedgeCount - 1) % m_window] >= required) {
            --m_wedgeCount;
        }
        // The back is worked out from the front and the count, never kept as
        // an index of its own: emptying the wedge from the back and leaving
        // a stale front is how a spike walks straight through.
        m_wedge[(m_wedgeFront + m_wedgeCount) % m_window] = position;
        ++m_wedgeCount;
    }

    /// The position \a index places behind the front of the wedge.
    [[nodiscard]] std::uint64_t at(std::uint32_t index) const noexcept RB_NONBLOCKING
    {
        return m_wedge[(m_wedgeFront + index) % m_window];
    }

    std::vector<float> m_delay;
    std::vector<float> m_required;
    /// Absolute sample positions, so that "older than the window" is a
    /// comparison and not a wrap-around puzzle.
    std::vector<std::uint64_t> m_wedge;
    TruePeakEstimator m_estimator;
    LinearSmoother m_ceilingGlide;
    double m_sampleRate = 48000.0;
    std::uint32_t m_channels = 0;
    std::uint32_t m_lookahead = 0;
    /// How much longer than the lookahead the signal is held: the true-peak
    /// estimate's own delay, or nothing.
    std::uint32_t m_detectionDelay = 0;
    /// The whole delay: the latency.
    std::uint32_t m_delayLength = 0;
    bool m_truePeakCapable = false;
    bool m_truePeak = false;
    std::uint32_t m_window = 0;
    std::uint32_t m_write = 0;
    std::uint32_t m_wedgeFront = 0;
    std::uint32_t m_wedgeCount = 0;
    std::uint64_t m_position = 0;
    float m_ceiling = 1.0f;
    double m_releaseCoefficient = 0.0;
    /// The gain before the averaging: the window's smallest, released.
    double m_held = 1.0;
    /// The last `lookahead` held gains, and their sum.
    std::vector<float> m_smoothing;
    double m_smoothSum = 0.0;
    std::uint32_t m_smoothIndex = 0;
    /// The gain applied: the held gains averaged.
    float m_gain = 1.0f;
    float m_lowestGain = 1.0f;
};

} // namespace rb::dsp

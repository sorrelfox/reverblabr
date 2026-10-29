// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/biquad.h"
#include "rt/realtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace rb::dsp
{

/// Loudness as ITU-R BS.1770 defines it: K-weighted mean square, in LUFS.
///
/// **K-weighting is two filters.** A high shelf of about +4 dB above 1.5 kHz
/// stands in for the head, and a high pass at about 38 Hz for how little the
/// ear makes of the lowest bass. Both are designed here from their analogue
/// prototypes for whatever rate the graph runs at, so that at 48 kHz they
/// come out as the recommendation's own coefficients (the unit test holds
/// them to those), and at 44.1 or 96 kHz they are the same filters rather
/// than 48 kHz numbers run at the wrong rate.

/// The two K-weighting stages for \a sampleRate: the shelf, then the high
/// pass.
[[nodiscard]] inline std::array<BiquadCoefficients, 2> kWeighting(double sampleRate) noexcept
{
    // The shelf: its corner, its gain in decibels and its Q, as the
    // recommendation's 48 kHz coefficients work out.
    constexpr double shelfHz = 1681.974450955533;
    constexpr double shelfGainDb = 3.999843853973347;
    constexpr double shelfQ = 0.7071752369554196;
    // How far the shelf's slope reaches into the band below it.
    constexpr double shelfBandExponent = 0.4996667741545416;
    // The high pass.
    constexpr double highPassHz = 38.13547087602444;
    constexpr double highPassQ = 0.5003270373238773;

    std::array<BiquadCoefficients, 2> stages;
    {
        const double k = std::tan(std::numbers::pi * shelfHz / sampleRate);
        const double vh = std::pow(10.0, shelfGainDb / 20.0);
        const double vb = std::pow(vh, shelfBandExponent);
        const double a0 = 1.0 + k / shelfQ + k * k;
        stages[0] = BiquadCoefficients{(vh + vb * k / shelfQ + k * k) / a0,
                                       2.0 * (k * k - vh) / a0,
                                       (vh - vb * k / shelfQ + k * k) / a0,
                                       2.0 * (k * k - 1.0) / a0,
                                       (1.0 - k / shelfQ + k * k) / a0};
    }
    {
        const double k = std::tan(std::numbers::pi * highPassHz / sampleRate);
        const double a0 = 1.0 + k / highPassQ + k * k;
        stages[1] = BiquadCoefficients{1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / highPassQ + k * k) / a0};
    }
    return stages;
}

/// A mean square as loudness units: −0.691 dB puts a 997 Hz sine at full
/// scale on one channel at −3.01 LUFS, which is what the recommendation's
/// calibration asks. Below kSilentLufs is silence.
inline constexpr float kSilentLufs = -120.0f;

[[nodiscard]] inline float lufsOfMeanSquare(double meanSquare) noexcept RB_NONBLOCKING
{
    if (!(meanSquare > 1.0e-15)) {
        return kSilentLufs;
    }
    return std::max(kSilentLufs, static_cast<float>(-0.691 + 10.0 * std::log10(meanSquare)));
}

/// The loudness of a stereo signal as a leveller wants it: BS.1770's gated
/// measure, over the last few seconds, worked out ten times a second.
///
/// **Blocks, as the recommendation measures a programme.** Every 100 ms the
/// last 400 ms is one block (the "momentary" loudness); the loudness is the
/// mean of the blocks of the last kWindowMs, leaving out those under −70
/// LUFS and then those more than 10 LU under the rest. So a breath between
/// sentences, or the gap between two words, does not read as the speaker
/// having gone quiet — which a plain 400 ms average does, and a leveller
/// reading it would lift every pause.
///
/// **Counted in samples**: each sample's K-weighted power goes into a 10 ms
/// slice, and the blocks are sums of slices, recomputed when one completes.
/// Nothing is per cycle, so it reads the same whatever the quantum, and
/// nothing is a running sum that adds and subtracts for hours and drifts.
///
/// The channels are weighted equally, as the recommendation weights left
/// and right.
class GatedLoudness
{
public:
    static constexpr float kSliceMs = 10.0f;
    static constexpr std::size_t kSlicesPerBlock = 40; ///< 400 ms.
    static constexpr std::size_t kSlicesPerHop = 10; ///< 100 ms.
    static constexpr std::size_t kBlocks = 27; ///< The blocks inside 3 s.
    static constexpr float kWindowMs = 3000.0f;
    static constexpr float kAbsoluteGateLufs = -70.0f;
    static constexpr float kRelativeGateLu = -10.0f;

    /// Designs the filters and sizes the slices. Allocates nothing, but is
    /// meant for prepare().
    void prepare(double sampleRate) noexcept
    {
        const std::array<BiquadCoefficients, 2> stages = kWeighting(sampleRate);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            m_shelf[channel].setCoefficients(stages[0]);
            m_highPass[channel].setCoefficients(stages[1]);
        }
        m_sliceSamples =
            std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(kSliceMs * 1.0e-3 * sampleRate)));
        reset();
    }

    /// Forgets everything: seconds of silence.
    void reset() noexcept RB_NONBLOCKING
    {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            m_shelf[channel].reset();
            m_highPass[channel].reset();
        }
        m_slices.fill(0.0);
        m_blocks.fill(0.0);
        m_nextSlice = 0;
        m_nextBlock = 0;
        m_sinceHop = 0;
        m_seen = 0;
        m_current = 0.0;
        m_counted = 0;
        m_momentary = kSilentLufs;
        m_loudness = kSilentLufs;
    }

    /// Takes one stereo frame. Returns true when a new block has been
    /// measured and the loudness worked out again.
    bool push(float left, float right) noexcept RB_NONBLOCKING
    {
        const double l = m_highPass[0].process(m_shelf[0].process(left));
        const double r = m_highPass[1].process(m_shelf[1].process(right));
        m_current += l * l + r * r;
        if (++m_counted < m_sliceSamples) {
            return false;
        }
        m_slices[m_nextSlice] = m_current;
        m_nextSlice = (m_nextSlice + 1) % kSlicesPerBlock;
        m_current = 0.0;
        m_counted = 0;
        m_seen = std::min(m_seen + 1, kSlicesPerBlock);
        if (++m_sinceHop < kSlicesPerHop) {
            return false;
        }
        m_sinceHop = 0;
        // **Not a block until there are 400 ms to make one.** The first
        // hundred milliseconds after a start would otherwise be measured
        // against the silence before it, and read 6 dB quieter than they
        // are — which a leveller would answer by lifting the first word.
        if (m_seen < kSlicesPerBlock) {
            return false;
        }

        double block = 0.0;
        for (const double slice : m_slices) {
            block += slice;
        }
        block /= static_cast<double>(kSlicesPerBlock * m_sliceSamples);
        m_momentary = lufsOfMeanSquare(block);
        m_blocks[m_nextBlock] = block;
        m_nextBlock = (m_nextBlock + 1) % kBlocks;
        m_loudness = gatedLoudness();
        return true;
    }

    /// The gated loudness of the last kWindowMs, in LUFS; kSilentLufs when
    /// every block of it is under the absolute gate.
    [[nodiscard]] float loudness() const noexcept RB_NONBLOCKING
    {
        return m_loudness;
    }

    /// The last 400 ms alone, ungated: BS.1770's momentary loudness.
    [[nodiscard]] float momentary() const noexcept RB_NONBLOCKING
    {
        return m_momentary;
    }

private:
    static constexpr std::size_t kChannels = 2;

    [[nodiscard]] double meanAbove(float gateLufs) const noexcept RB_NONBLOCKING
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (const double block : m_blocks) {
            if (lufsOfMeanSquare(block) > gateLufs) {
                sum += block;
                ++count;
            }
        }
        return count == 0 ? 0.0 : sum / static_cast<double>(count);
    }

    [[nodiscard]] float gatedLoudness() const noexcept RB_NONBLOCKING
    {
        const double absolute = meanAbove(kAbsoluteGateLufs);
        if (!(absolute > 0.0)) {
            return kSilentLufs;
        }
        return lufsOfMeanSquare(meanAbove(lufsOfMeanSquare(absolute) + kRelativeGateLu));
    }

    std::array<Biquad, kChannels> m_shelf;
    std::array<Biquad, kChannels> m_highPass;
    std::array<double, kSlicesPerBlock> m_slices{};
    std::array<double, kBlocks> m_blocks{};
    std::size_t m_nextSlice = 0;
    std::size_t m_nextBlock = 0;
    std::size_t m_sinceHop = 0;
    /// Slices since the last reset, up to a block's worth.
    std::size_t m_seen = 0;
    double m_current = 0.0;
    std::uint32_t m_counted = 0;
    std::uint32_t m_sliceSamples = 480;
    float m_momentary = kSilentLufs;
    float m_loudness = kSilentLufs;
};

} // namespace rb::dsp

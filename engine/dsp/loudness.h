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
#include <limits>
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

/// What a leveller does about a reading: how far it answers each way, from
/// where, and how far it may go. All in decibels or loudness units; a slope
/// of 1 corrects the whole distance and a slope of 0 none of it.
struct PivotLaw {
    /// How much of the distance past the band is taken off a loud reading.
    float cutSlope = 1.0f;
    /// How much of the distance under the band is added to a quiet one.
    float liftSlope = 1.0f;
    /// The band that is left alone: this far over the pivot...
    float bandAboveLu = 0.0f;
    /// ...and this far under it.
    float bandBelowLu = 0.0f;
    /// A reading further under the pivot than this, a negative number, is
    /// not lifted at all: nothing that faint is worth bringing up.
    float floorLu = -std::numeric_limits<float>::infinity();
    /// The most it may take off, and the most it may add.
    float mostCutDb = std::numeric_limits<float>::infinity();
    float mostLiftDb = std::numeric_limits<float>::infinity();
};

/// The gain, in dB of change, that \a law asks for when \a readingLufs is
/// measured against \a pivotLufs. The curve has no step at the band's edge.
/// What goes in as the reading, and whether a leveller trusts it at all (its
/// gate, its two readings for its two directions), is the leveller's.
[[nodiscard]] inline float pivotGainDb(float readingLufs, float pivotLufs, const PivotLaw &law) noexcept RB_NONBLOCKING
{
    const float distance = readingLufs - pivotLufs;
    float gain = 0.0f;
    if (distance > law.bandAboveLu) {
        gain = 0.0f - (distance - law.bandAboveLu) * law.cutSlope;
    } else if (distance < -law.bandBelowLu && distance >= law.floorLu) {
        gain = (-distance - law.bandBelowLu) * law.liftSlope;
    }
    return std::clamp(gain, -law.mostCutDb, law.mostLiftDb);
}

/// The first steps of a BS.1770 measure, for whichever way it goes on to
/// use them: each frame K-weighted, its two sides' powers added, gathered
/// into 10 ms slices. Counted in samples, so a slice ends in the same place
/// whatever the quantum. The channels are weighted equally, as the
/// recommendation weights left and right.
class KWeightedSlices
{
public:
    static constexpr float kSliceMs = 10.0f;

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

    void reset() noexcept RB_NONBLOCKING
    {
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            m_shelf[channel].reset();
            m_highPass[channel].reset();
        }
        m_current = 0.0;
        m_finished = 0.0;
        m_counted = 0;
    }

    /// Takes one stereo frame. Returns true when it ended a slice.
    bool push(float left, float right) noexcept RB_NONBLOCKING
    {
        const double l = m_highPass[0].process(m_shelf[0].process(left));
        const double r = m_highPass[1].process(m_shelf[1].process(right));
        m_current += l * l + r * r;
        if (++m_counted < m_sliceSamples) {
            return false;
        }
        m_finished = m_current;
        m_current = 0.0;
        m_counted = 0;
        return true;
    }

    /// The power summed over the slice that ended last.
    [[nodiscard]] double finished() const noexcept RB_NONBLOCKING
    {
        return m_finished;
    }

    [[nodiscard]] std::uint32_t sliceSamples() const noexcept RB_NONBLOCKING
    {
        return m_sliceSamples;
    }

private:
    static constexpr std::size_t kChannels = 2;

    std::array<Biquad, kChannels> m_shelf;
    std::array<Biquad, kChannels> m_highPass;
    double m_current = 0.0;
    double m_finished = 0.0;
    std::uint32_t m_counted = 0;
    std::uint32_t m_sliceSamples = 480;
};

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
/// **A quicker reading beside it,** fast(): the last 100 ms alone, worked out
/// with each slice, for what must not wait a tenth of a second to hear that
/// something got loud.
class GatedLoudness
{
public:
    static constexpr float kSliceMs = KWeightedSlices::kSliceMs;
    static constexpr std::size_t kSlicesPerBlock = 40; ///< 400 ms.
    static constexpr std::size_t kSlicesPerHop = 10; ///< 100 ms.
    static constexpr std::size_t kSlicesPerFast = 10; ///< 100 ms.
    static constexpr std::size_t kBlocks = 27; ///< The blocks inside 3 s.
    static constexpr float kWindowMs = 3000.0f;
    static constexpr float kAbsoluteGateLufs = -70.0f;
    static constexpr float kRelativeGateLu = -10.0f;

    /// Designs the filters and sizes the slices. Allocates nothing, but is
    /// meant for prepare().
    void prepare(double sampleRate) noexcept
    {
        m_weighted.prepare(sampleRate);
        reset();
    }

    /// Forgets everything: seconds of silence.
    void reset() noexcept RB_NONBLOCKING
    {
        m_weighted.reset();
        m_slices.fill(0.0);
        m_blocks.fill(0.0);
        m_nextSlice = 0;
        m_nextBlock = 0;
        m_sinceHop = 0;
        m_seen = 0;
        m_sliceEnded = false;
        m_momentary = kSilentLufs;
        m_fast = kSilentLufs;
        m_loudness = kSilentLufs;
    }

    /// Takes one stereo frame. Returns true when a new block has been
    /// measured and the loudness worked out again.
    bool push(float left, float right) noexcept RB_NONBLOCKING
    {
        m_sliceEnded = m_weighted.push(left, right);
        if (!m_sliceEnded) {
            return false;
        }
        m_slices[m_nextSlice] = m_weighted.finished();
        m_nextSlice = (m_nextSlice + 1) % kSlicesPerBlock;
        m_seen = std::min(m_seen + 1, kSlicesPerBlock);
        m_fast = fastLoudness();
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
        block /= static_cast<double>(kSlicesPerBlock * m_weighted.sliceSamples());
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

    /// The last 100 ms alone, ungated, new every 10 ms. kSilentLufs until
    /// there are 100 ms to measure, for the reason a block waits for its 400.
    [[nodiscard]] float fast() const noexcept RB_NONBLOCKING
    {
        return m_fast;
    }

    /// Whether the push() just made ended a 10 ms slice, and fast() is new.
    /// push() itself answers only for the blocks.
    [[nodiscard]] bool sliceEnded() const noexcept RB_NONBLOCKING
    {
        return m_sliceEnded;
    }

private:
    [[nodiscard]] float fastLoudness() const noexcept RB_NONBLOCKING
    {
        if (m_seen < kSlicesPerFast) {
            return kSilentLufs;
        }
        // Summed afresh each time, like a block, so nothing drifts.
        double sum = 0.0;
        for (std::size_t back = 1; back <= kSlicesPerFast; ++back) {
            sum += m_slices[(m_nextSlice + kSlicesPerBlock - back) % kSlicesPerBlock];
        }
        return lufsOfMeanSquare(sum / static_cast<double>(kSlicesPerFast * m_weighted.sliceSamples()));
    }

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

    KWeightedSlices m_weighted;
    std::array<double, kSlicesPerBlock> m_slices{};
    std::array<double, kBlocks> m_blocks{};
    std::size_t m_nextSlice = 0;
    std::size_t m_nextBlock = 0;
    std::size_t m_sinceHop = 0;
    /// Slices since the last reset, up to a block's worth.
    std::size_t m_seen = 0;
    bool m_sliceEnded = false;
    float m_momentary = kSilentLufs;
    float m_fast = kSilentLufs;
    float m_loudness = kSilentLufs;
};

/// The level of a voice over the last seconds of voice: the same K-weighted
/// 400 ms blocks at the same 100 ms hop as GatedLoudness, in the same units,
/// but a block counts only if a voice was in it.
///
/// **It stands still in a pause.** GatedLoudness's blocks run on the clock,
/// so three seconds into a pause it reads the room, and its relative gate
/// holds off a real fall for most of its ring. Here the ring moves in
/// seconds of voice: with no voice nothing goes in, so the reading is simply
/// not touched. That is no timer and no peak hold, and it keeps numbers, never
/// sound.
///
/// **The detector stands in for the relative gate.** It is the caller's, so
/// that this needs no model: VoiceLikelihood::voiced() for each frame.
///
/// **A long silence starts it again,** so an old turn does not mix with a new
/// one.
class VoicedLoudness
{
public:
    static constexpr std::size_t kSlicesPerBlock = 40; ///< 400 ms.
    static constexpr std::size_t kSlicesPerHop = 10; ///< 100 ms.
    /// A block counts when this many of its slices hold a voice: 60 %.
    static constexpr std::size_t kLeastVoicedSlices = 24;
    static constexpr std::size_t kBlocks = 197; ///< The blocks inside 20 s.
    static constexpr std::size_t kFirstReadingBlocks = 7; ///< The blocks inside 1 s.
    static constexpr float kDefaultReadingSeconds = 3.0f;
    static constexpr float kDefaultForgetSeconds = 60.0f;

    /// Designs the filters and sizes the slices. Allocates nothing, but is
    /// meant for prepare().
    void prepare(double sampleRate) noexcept
    {
        m_weighted.prepare(sampleRate);
        reset();
    }

    /// Forgets everything that was heard. The settings stay.
    void reset() noexcept RB_NONBLOCKING
    {
        m_weighted.reset();
        m_slices.fill(0.0);
        m_voicedSlices.fill(false);
        m_nextSlice = 0;
        m_seen = 0;
        m_sinceHop = 0;
        m_quietSlices = 0;
        forget();
    }

    /// How many seconds of voice the reading is the mean of, 1 at the
    /// least. It answers at once, in a pause too.
    void setReadingSeconds(float seconds) noexcept RB_NONBLOCKING
    {
        // A second of voice is the seven blocks inside it, three seconds
        // the 27 GatedLoudness has: each block adds a hop to the first 400 ms.
        const long blocks = std::lround(seconds * 10.0f) - 3;
        m_readingBlocks = static_cast<std::size_t>(
            std::clamp(blocks, static_cast<long>(kFirstReadingBlocks), static_cast<long>(kBlocks)));
        if (m_length >= kFirstReadingBlocks) {
            m_reading = meanOfLast(m_readingBlocks);
        }
    }

    /// How long without a voice before all of it is forgotten.
    void setForgetSeconds(float seconds) noexcept RB_NONBLOCKING
    {
        m_forgetSlices =
            static_cast<std::uint32_t>(std::max(1L, std::lround(seconds * 1000.0f / KWeightedSlices::kSliceMs)));
    }

    /// Takes one stereo frame, and whether the detector's latest frame was
    /// voiced. A VoiceLikelihood prepared for the same rate ends its frames
    /// on the same samples, so pushed first, it speaks for this very slice.
    void push(float left, float right, bool voiced) noexcept RB_NONBLOCKING
    {
        if (!m_weighted.push(left, right)) {
            return;
        }
        m_slices[m_nextSlice] = m_weighted.finished();
        m_voicedSlices[m_nextSlice] = voiced;
        m_nextSlice = (m_nextSlice + 1) % kSlicesPerBlock;
        m_seen = std::min(m_seen + 1, kSlicesPerBlock);
        m_quietSlices = voiced ? 0 : std::min(m_quietSlices + 1, m_forgetSlices);
        if (m_quietSlices >= m_forgetSlices) {
            forget();
        }
        if (++m_sinceHop < kSlicesPerHop) {
            return;
        }
        m_sinceHop = 0;
        const auto voicedSlices = static_cast<std::size_t>(std::ranges::count(m_voicedSlices, true));
        if (m_seen == kSlicesPerBlock && voicedSlices >= kLeastVoicedSlices) {
            addBlock();
        }
    }

    /// The voice's level over the last seconds of voice, in LUFS. It is held
    /// through a pause, and kSilentLufs until 1 s of voice has been heard.
    [[nodiscard]] float reading() const noexcept RB_NONBLOCKING
    {
        return m_reading;
    }

    /// The level over the whole ring, 20 s of voice, in LUFS; kSilentLufs
    /// until the ring is full.
    [[nodiscard]] float wholeLevel() const noexcept RB_NONBLOCKING
    {
        return m_wholeLevel;
    }

    /// How many blocks of voice are held, each a tenth of a second of it.
    [[nodiscard]] std::size_t blocks() const noexcept RB_NONBLOCKING
    {
        return m_length;
    }

private:
    void forget() noexcept RB_NONBLOCKING
    {
        m_nextBlock = 0;
        m_length = 0;
        m_reading = kSilentLufs;
        m_wholeLevel = kSilentLufs;
    }

    void addBlock() noexcept RB_NONBLOCKING
    {
        double block = 0.0;
        for (const double slice : m_slices) {
            block += slice;
        }
        m_blocks[m_nextBlock] = block / static_cast<double>(kSlicesPerBlock * m_weighted.sliceSamples());
        m_nextBlock = (m_nextBlock + 1) % kBlocks;
        m_length = std::min(m_length + 1, kBlocks);
        if (m_length >= kFirstReadingBlocks) {
            m_reading = meanOfLast(m_readingBlocks);
        }
        if (m_length == kBlocks) {
            m_wholeLevel = meanOfLast(kBlocks);
        }
    }

    /// The mean of the newest \a blocks, or of as many as there are. Summed
    /// afresh each time, so nothing drifts.
    [[nodiscard]] float meanOfLast(std::size_t blocks) const noexcept RB_NONBLOCKING
    {
        const std::size_t count = std::min(blocks, m_length);
        double sum = 0.0;
        for (std::size_t back = 1; back <= count; ++back) {
            sum += m_blocks[(m_nextBlock + kBlocks - back) % kBlocks];
        }
        return lufsOfMeanSquare(sum / static_cast<double>(count));
    }

    KWeightedSlices m_weighted;
    std::array<double, kSlicesPerBlock> m_slices{};
    std::array<bool, kSlicesPerBlock> m_voicedSlices{};
    std::array<double, kBlocks> m_blocks{};
    std::size_t m_nextSlice = 0;
    std::size_t m_nextBlock = 0;
    std::size_t m_seen = 0;
    std::size_t m_sinceHop = 0;
    /// How many blocks are in the ring.
    std::size_t m_length = 0;
    std::size_t m_readingBlocks = 27;
    std::uint32_t m_quietSlices = 0;
    std::uint32_t m_forgetSlices = 6000;
    float m_reading = kSilentLufs;
    float m_wholeLevel = kSilentLufs;
};

} // namespace rb::dsp

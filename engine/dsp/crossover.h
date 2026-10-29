// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audioblock.h"
#include "dsp/biquad.h"
#include "rt/realtime.h"

#include <cstdint>
#include <vector>

namespace rb::dsp
{

/// How many bands ThreeBandCrossover makes.
inline constexpr std::uint32_t kCrossoverBands = 3;

/// The closest the two splits may come: the upper one at least this many
/// times the lower. Two Linkwitz–Riley splits much nearer than that leave a
/// middle band with hardly anything in it, and one that dips.
inline constexpr double kMinCrossoverSpacing = 1.5;

/// How long a split takes to glide to a frequency it is moved to.
inline constexpr double kCrossoverGlideMs = 30.0;

/// The splits \a lowHz and \a highHz as a crossover at \a sampleRate uses
/// them: each inside what a filter at that rate can be set to, the upper at
/// least kMinCrossoverSpacing times the lower, and the lower moved down
/// rather than the upper past what the rate allows.
struct CrossoverSplits {
    double lowHz = 200.0;
    double highHz = 3000.0;
};
[[nodiscard]] CrossoverSplits
clampCrossoverSplits(double lowHz, double highHz, double sampleRate) noexcept RB_NONBLOCKING;

/// Three bands out of one signal, which add back up to it.
///
/// **Two fourth-order Linkwitz–Riley splits**, each a pair of Butterworth
/// sections a side: 24 dB an octave, and −6 dB for both halves at the split,
/// so the halves add up with no bump and no dip. The lower split makes the
/// low band and the rest; the upper split divides the rest into the middle
/// and the high band.
///
/// **The low band goes through an all-pass at the upper split.** The two
/// halves of a Linkwitz–Riley split add up to an all-pass, not to the input,
/// so the middle and the high band together have turned by the upper split's
/// all-pass; the low band, which never went through that split, has to turn
/// by it too, or the three would add up with a dip where they meet. With it,
/// all three add up to the two all-passes one after the other: every
/// frequency at exactly 0 dB, only the phase moved.
///
/// **State-variable filters**, so the splits can move while sound goes
/// through: a split that is moved glides there over kCrossoverGlideMs, its
/// filters worked out afresh each sample on the way, which a direct-form
/// filter would not survive without a thump.
class ThreeBandCrossover
{
public:
    /// Sizes the filters for \a channels. **Not for the audio thread.** The
    /// splits are wherever setSplits() last put them, at once.
    void prepare(double sampleRate, std::uint32_t channels);

    /// Where the splits should be. They glide there from where they are; see
    /// clampCrossoverSplits() for what is done to frequencies that cannot be.
    void setSplits(double lowHz, double highHz) noexcept RB_NONBLOCKING;

    /// Forgets the signal, and puts the splits where they are going at once.
    void reset() noexcept RB_NONBLOCKING;

    /// The splits as they are this moment, which during a glide is on the way.
    [[nodiscard]] CrossoverSplits splits() const noexcept RB_NONBLOCKING;

    /// Splits \a frames of \a input into \a low, \a mid and \a high. Each
    /// band may be the input itself: every sample is read before the bands
    /// are written. Channels beyond the fewest any of the four has are left
    /// alone. Real-time: bounded work, no allocation.
    void process(const AudioBlock &input,
                 const AudioBlock &low,
                 const AudioBlock &mid,
                 const AudioBlock &high,
                 std::uint32_t frames) noexcept RB_NONBLOCKING;

private:
    /// One channel's filters: four sections at each split, and the low
    /// band's all-pass.
    struct Channel {
        Svf lowSplitLow1;
        Svf lowSplitLow2;
        Svf lowSplitHigh1;
        Svf lowSplitHigh2;
        Svf highSplitLow1;
        Svf highSplitLow2;
        Svf highSplitHigh1;
        Svf highSplitHigh2;
        Svf lowBandAllPass;
    };

    /// One split's coefficients: the same two integrators give the low
    /// half, the high half and the all-pass, only the output mix differs.
    struct Split {
        SvfCoefficients low;
        SvfCoefficients high;
        SvfCoefficients allPass;
    };

    void design(Split &split, double hz) const noexcept RB_NONBLOCKING;
    void designBoth() noexcept RB_NONBLOCKING;
    /// Moves the splits one sample along their glide. Returns whether they
    /// moved, and so whether the coefficients need working out again.
    [[nodiscard]] bool glide() noexcept RB_NONBLOCKING;

    double m_sampleRate = 48000.0;
    std::vector<Channel> m_channels;

    /// What setSplits() was last asked for, kept so that prepare() can bring
    /// it inside a rate that was not known when it was asked.
    double m_askedLowHz = 200.0;
    double m_askedHighHz = 3000.0;

    Split m_lowSplit;
    Split m_highSplit;

    /// The splits in octaves (log2 of hertz), where a glide is even: an
    /// octave takes as long at 100 Hz as at 5 kHz.
    double m_lowOctave = 0.0;
    double m_highOctave = 0.0;
    double m_lowTarget = 0.0;
    double m_highTarget = 0.0;
    /// How much of the way the glide goes each sample.
    double m_glideStep = 0.0;
};

} // namespace rb::dsp

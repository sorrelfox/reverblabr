// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/fft.h"
#include "rt/realtime.h"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::dsp
{

/// Softens what rises far above the running level, one range of the sound at
/// a time, and leaves the level itself alone.
///
/// **The method** is the multi-channel one of Keshavarzi, Baer and Moore
/// ("Evaluation of a multi-channel algorithm for reducing transient sounds",
/// International Journal of Audiology 57(8), 2018), taken to desktop rates.
/// A tapered frame of 1 ms every 0.46 ms goes through a zero-padded transform
/// and its bins are grouped into five ranges. Each range is compared with its
/// own recent past, and a range that stands more than its threshold above that
/// past loses a share of the excess, in dB. The central samples of a frame are
/// the output and the frames are joined with no overlap, so a frame with no
/// cut is the input to the bit.
///
/// **Seven things differ from that paper**, because on a desktop it does not
/// work as printed:
/// - Ranges 4 and 5 have higher thresholds: consonants fired them.
/// - The strongest setting's share is the paper's own "perhaps", not its
///   printed one.
/// - Frames under kQuietDb neither trigger nor enter the past, and a range
///   needs kLearnMs of sound before any cut. A digital stream falls to exact
///   zero, its past falls with it, and the next word would look sudden.
/// - A rise that stays is left alone after kHoldMs: a cut frame does not enter
///   the past, so a step up would otherwise stay cut for ever.
/// - No cut passes kMostCutDb.
/// - The past is the root of a mean of power, not a mean of magnitudes. It
///   reads higher, so the same threshold fires less.
/// - The end of that hold lets go over kLetGoMs, not in one frame.
///
/// One thing more than those: the hold is time since the rise, and a dip under
/// kOverEndsMs does not restart it, or a narrow range, or a sound with gaps in
/// it, would stay cut long after kHoldMs.
///
/// **kThresholds are provisional.** They come from a few voices, one guitar
/// and synthetic knocks, and have not been set on recordings.
///
/// **Above the top range** there is no detecting, since the paper's rate did
/// not reach so high. Those bins take the top range's cut, so that a cut leaves
/// no sound standing alone.
///
/// **Strength sets depth and not how often**: the share appears only in the
/// size of a cut, so it starts well above nothing, at the threshold's share.
///
/// **A cut can raise a peak.** Taking a range out of a waveform changes its
/// shape, so a block built on this adds to its reach even though no range
/// ever gains.
///
/// process() takes samples one at a time against a ring, so the sound does not
/// depend on how the cycles fall. The delay is latencySamples(), whatever the
/// share, so a change of Strength never moves a lane.
class TransientCut
{
public:
    static constexpr std::size_t kRanges = 5;
    /// How far a range has to stand above its past, as a ratio of magnitudes.
    /// Provisional: see the class comment.
    static constexpr std::array<float, kRanges> kThresholds{12.0f, 21.0f, 12.0f, 12.0f, 16.0f};
    /// Where each range ends, in Hz. Each falls between two bins at all the
    /// usual rates, which keeps the paper's 1, 1, 2, 3 and 9 bins at 44.1 kHz.
    static constexpr std::array<double, kRanges> kRangeEndsHz{375.0, 1125.0, 2625.0, 4700.0, 10875.0};

    static constexpr double kFrameMs = 1.0;
    static constexpr double kHopMs = 0.46;
    static constexpr double kPastMs = 680.0;
    static constexpr double kHoldMs = 200.0;
    static constexpr double kLearnMs = 100.0;
    static constexpr double kLetGoMs = 50.0;
    static constexpr double kOverEndsMs = 50.0;
    static constexpr float kQuietDb = -60.0f;
    static constexpr float kMostCutDb = 15.0f;

    /// Sizes everything for \a sampleRate. Allocates: not for the audio
    /// thread. Starts with a share of 0.
    void prepare(double sampleRate);

    /// How much of a range's excess is taken off, in dB per dB. 0 stops any
    /// new cut, and one under way lets go; the past goes on being kept, so
    /// choosing a share later needs no time to learn.
    void setShare(float share) noexcept RB_NONBLOCKING
    {
        m_share = share;
    }

    /// Forgets everything heard. The share stays.
    void reset() noexcept RB_NONBLOCKING;

    /// How late the output is: the frame less one taper.
    [[nodiscard]] std::uint32_t latencySamples() const noexcept RB_NONBLOCKING
    {
        return m_frame - m_taper;
    }

    /// The samples between two frames.
    [[nodiscard]] std::uint32_t hopSamples() const noexcept RB_NONBLOCKING
    {
        return m_hop;
    }

    /// The cut on range \a range in the last frame, in dB, 0 or more.
    [[nodiscard]] float cutDb(std::size_t range) const noexcept RB_NONBLOCKING
    {
        return m_cut[range];
    }

    /// The deepest of them.
    [[nodiscard]] float deepestCutDb() const noexcept RB_NONBLOCKING;

    /// Runs \a samples in place, and calls \a onFrame() after each frame.
    template <typename OnFrame>
    void process(std::span<float> samples, OnFrame &&onFrame) noexcept RB_NONBLOCKING
    {
        for (float &sample : samples) {
            const float in = sample;
            sample = m_out[m_outAt];
            m_ring[m_ringAt] = in;
            m_ringAt = m_ringAt + 1 == m_frame ? 0 : m_ringAt + 1;
            if (++m_outAt == m_hop) {
                m_outAt = 0;
                runFrame();
                onFrame();
            }
        }
    }

    /// Runs \a samples in place.
    void process(std::span<float> samples) noexcept RB_NONBLOCKING
    {
        process(samples, []() noexcept RB_NONBLOCKING { });
    }

private:
    /// One range's past: the last few powers in a ring, and their sum.
    struct Past {
        std::size_t offset = 0;
        std::uint32_t count = 0;
        std::uint32_t at = 0;
        double sum = 0.0;
        /// The same sum from the ring's start, taken over at each lap, so
        /// rounding has one lap to gather in however long a session runs.
        double fresh = 0.0;
    };

    /// The bins of a range, as first and one past the last.
    struct Bins {
        std::uint32_t first = 0;
        std::uint32_t end = 0;
    };

    void push(std::size_t range, float power) noexcept RB_NONBLOCKING;
    void runFrame() noexcept RB_NONBLOCKING;

    std::uint32_t m_frame = 1;
    std::uint32_t m_hop = 1;
    std::uint32_t m_taper = 0;
    std::uint32_t m_pastFrames = 1;
    std::uint32_t m_holdFrames = 1;
    std::uint32_t m_learnFrames = 1;
    std::uint32_t m_overEndsFrames = 1;
    float m_letGoStepDb = 0.0f;
    double m_quietPower = 0.0;
    float m_share = 0.0f;

    RealFft m_fft;
    std::vector<float> m_window;
    std::vector<float> m_ring;
    std::vector<float> m_ordered;
    std::vector<float> m_padded;
    std::vector<float> m_out;
    std::vector<std::complex<float>> m_spectrum;
    std::vector<float> m_history;
    std::uint32_t m_ringAt = 0;
    std::uint32_t m_outAt = 0;

    std::array<Bins, kRanges> m_bins{};
    std::array<Past, kRanges> m_past{};
    std::array<float, kRanges> m_cut{};
    std::array<std::uint32_t, kRanges> m_over{};
    std::array<std::uint32_t, kRanges> m_under{};
    std::array<float, kRanges> m_heldCut{};
    std::array<bool, kRanges> m_lettingGo{};
};

} // namespace rb::dsp

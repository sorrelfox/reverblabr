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
#include <span>
#include <vector>

namespace rb::dsp
{

/// Finds what is alike on both sides of a stereo sound.
///
/// **What it measures.** For every band of a short spectrum it keeps the power
/// of each side and the real part of their product, smoothed over about 21 ms.
/// Their ratio is 1 where the two sides move together, 0 where they have
/// nothing to do with each other, and under 0 where they oppose: the last is
/// clipped, so anything in opposite phase counts as not alike. The ratio is
/// taken to the 16th power, which keeps only what is very nearly alike.
///
/// **What it gives.** That mask on the two sides' mean, back in time, one
/// signal: the centre. It works from the phase and not from the balance, so a
/// source panned part-way in the same phase scores as centre too.
///
/// **It takes nothing from the sound it hands back.** The input comes out
/// delayed by exactly as long as the centre takes, so a caller can write
/// `rest = delayed - centre` and have the two add up to the input by
/// construction, and at no cut the input itself, untouched by any transform.
///
/// **A frame's delay, whatever the cycle.** A hop's output is ready only after
/// a whole frame has covered it, and a block has to sound the same at any
/// cycle length, so a whole frame is held back. The sqrt-Hann window and
/// overlap-add are those of the Denoise block.
class CentreSplit
{
public:
    /// The frame up to kMostRateOfOneFrame. Above it the frame doubles, to
    /// keep the width of a band and the delay.
    static constexpr std::size_t kFrameSize = 1024;
    static constexpr std::size_t kHopsPerFrame = 4;
    static constexpr double kMostRateOfOneFrame = 50000.0;
    /// How much of the last hop's power a band keeps, at 48 kHz.
    static constexpr float kSmoothingAt48k = 0.775f;
    /// The band the mask covers: outside it nothing is centre.
    static constexpr float kLowestHz = 100.0f;
    static constexpr float kHighestHz = 12000.0f;
    /// The band centrePower() and widePower() read, where voices are.
    static constexpr float kVoiceLowestHz = 300.0f;
    static constexpr float kVoiceHighestHz = 3400.0f;

    /// One sample's worth: the centre, and the input it is lined up with.
    struct Parts {
        float centre = 0.0f;
        float left = 0.0f;
        float right = 0.0f;
    };

    /// Sizes everything for \a sampleRate. **Not for the audio thread.**
    void prepare(double sampleRate);

    /// Forgets the signal. Real-time: it writes zeros.
    void reset() noexcept RB_NONBLOCKING;

    /// Takes one stereo frame and hands back the one a frame ago.
    [[nodiscard]] Parts push(float left, float right) noexcept RB_NONBLOCKING
    {
        const std::size_t slot = m_position - (m_frameSize - m_hop);
        const Parts parts{m_ready[slot], m_delayed[0][m_nextDelayed], m_delayed[1][m_nextDelayed]};
        m_delayed[0][m_nextDelayed] = left;
        m_delayed[1][m_nextDelayed] = right;
        m_nextDelayed = m_nextDelayed + 1 == m_frameSize ? 0 : m_nextDelayed + 1;
        m_input[0][m_position] = left;
        m_input[1][m_position] = right;
        m_hopEnded = ++m_position == m_frameSize;
        if (m_hopEnded) {
            processFrame();
            m_position = m_frameSize - m_hop;
        }
        return parts;
    }

    /// Whether the last push() ended a hop: a new mask and new powers.
    [[nodiscard]] bool hopEnded() const noexcept RB_NONBLOCKING
    {
        return m_hopEnded;
    }

    /// How many samples the centre and the input are held back by.
    [[nodiscard]] std::size_t frameSize() const noexcept RB_NONBLOCKING
    {
        return m_frameSize;
    }

    [[nodiscard]] std::size_t hop() const noexcept RB_NONBLOCKING
    {
        return m_hop;
    }

    /// The last hop's mask, band by band, 0 to 1.
    [[nodiscard]] std::span<const float> mask() const noexcept RB_NONBLOCKING
    {
        return m_mask;
    }

    /// The power of the last frame's centre in the voice band, where a
    /// full-scale tone on both sides reads 1.
    [[nodiscard]] float centrePower() const noexcept RB_NONBLOCKING
    {
        return m_centrePower;
    }

    /// The same for the mean power of the two sides, centre and all.
    [[nodiscard]] float widePower() const noexcept RB_NONBLOCKING
    {
        return m_widePower;
    }

private:
    static constexpr std::size_t kSides = 2;

    /// Both sides into the frequency domain, the mask, and back.
    void processFrame() noexcept RB_NONBLOCKING;

    RealFft m_fft;
    std::size_t m_frameSize = kFrameSize;
    std::size_t m_hop = kFrameSize / kHopsPerFrame;
    std::size_t m_firstBin = 0;
    std::size_t m_endBin = 0;
    std::size_t m_voiceFirstBin = 0;
    std::size_t m_voiceEndBin = 0;
    float m_smoothing = kSmoothingAt48k;
    /// What makes a full-scale tone's power 1.
    float m_powerScale = 1.0f;

    std::vector<float> m_window;
    std::array<std::vector<float>, kSides> m_input;
    std::array<std::vector<float>, kSides> m_delayed;
    std::size_t m_nextDelayed = 0;
    std::vector<float> m_frame;
    std::array<std::vector<std::complex<float>>, kSides> m_spectrum;
    std::vector<std::complex<float>> m_centre;
    std::vector<float> m_output;
    std::vector<float> m_ready;
    std::size_t m_position = 0;
    bool m_hopEnded = false;

    /// Per band: the smoothed power of each side and of the two together.
    std::vector<float> m_leftPower;
    std::vector<float> m_rightPower;
    std::vector<float> m_bothPower;
    std::vector<float> m_mask;
    float m_centrePower = 0.0f;
    float m_widePower = 0.0f;
};

} // namespace rb::dsp

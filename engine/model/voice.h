// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/fft.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::model
{

// **The shape of a voice conversion model** (ADR-0031). These are what such
// models were trained on, so they are the same for every folder: sound at
// 16 kHz, a frame of features every 20 ms, doubled to the voice's 10 ms.

inline constexpr std::uint32_t kVoiceInputRate = 16000;
/// One of the voice's frames, in samples of the input.
inline constexpr std::uint32_t kVoiceFrame = 160;
/// One frame of the content encoder's features. A hop and a window are whole
/// numbers of these, so a run's frames fall where the last run's did.
inline constexpr std::uint32_t kVoiceContentFrame = 2 * kVoiceFrame;
/// The pitch estimator's input: bands of a spectrum, and its padding.
inline constexpr std::size_t kVoiceBands = 128;
inline constexpr std::size_t kVoiceBandFrames = 32;
/// The pitches it tells apart, a fifth of a semitone apart.
inline constexpr std::size_t kVoicePitches = 360;
/// Numbers of the noise the voice takes for each frame.
inline constexpr std::size_t kVoiceNoiseSize = 192;

/// How many of the voice's frames a window of \a samples yields. The content
/// encoder's first frame needs more sound than the rest, which costs it one.
[[nodiscard]] constexpr std::uint32_t voiceFramesIn(std::uint32_t samples) noexcept
{
    return 2 * (samples / kVoiceContentFrame - 1);
}

/// **Turns sound into the log spectrum the pitch estimator was trained on**:
/// a window of 64 ms every 10 ms, centred, on the sound mirrored at its ends,
/// in 128 bands from 30 Hz to 8 kHz. Off the audio thread.
class VoiceBands
{
public:
    VoiceBands();

    /// How many frames \a samples of sound give.
    [[nodiscard]] static std::size_t frames(std::size_t samples) noexcept;

    /// The frames of \a sound, band after band: \a bands holds frames() times
    /// kVoiceBands values.
    void analyse(std::span<const float> sound, std::span<float> bands);

private:
    /// A band's weights over the bins of the spectrum.
    struct Band {
        std::size_t first = 0;
        std::vector<float> weights;
    };

    dsp::RealFft m_fft;
    std::vector<float> m_window;
    std::vector<float> m_frame;
    std::vector<std::complex<float>> m_spectrum;
    std::vector<float> m_magnitude;
    std::vector<Band> m_bands;
};

/// The pitch a frame of the estimator's output says, in Hz, or 0 where it
/// hears none. \a likelihoods holds kVoicePitches values.
[[nodiscard]] float pitchFromLikelihoods(std::span<const float> likelihoods) noexcept;

/// \a hertz moved by \a cents. No pitch stays no pitch.
[[nodiscard]] float shiftedPitch(float hertz, float cents) noexcept;

/// \a hertz on the voice's scale of 255 steps, where 1 is no pitch.
[[nodiscard]] std::int64_t pitchStep(float hertz) noexcept;

} // namespace rb::model

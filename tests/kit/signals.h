// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <vector>

namespace rb::test
{

/// One channel of audio, as long as the test needs.
using Signal = std::vector<float>;

/// Several channels of the same length.
using MultiSignal = std::vector<Signal>;

/// The rate the generators use unless a test says otherwise.
inline constexpr double kTestSampleRate = 48000.0;

/// \a frames frames of nothing.
[[nodiscard]] Signal silence(std::size_t frames);

/// \a frames frames all of \a value.
[[nodiscard]] Signal constant(std::size_t frames, float value);

/// A sine of \a amplitude at \a frequencyHz, starting at \a phase radians.
[[nodiscard]] Signal sine(std::size_t frames,
                          double frequencyHz,
                          double sampleRate = kTestSampleRate,
                          float amplitude = 1.0f,
                          double phase = 0.0);

/// A sweep from \a startHz to \a endHz, logarithmic in frequency, which is
/// what a frequency response wants: every octave gets the same number of
/// frames.
[[nodiscard]] Signal
sweep(std::size_t frames, double startHz, double endHz, double sampleRate = kTestSampleRate, float amplitude = 1.0f);

/// Uniform noise between -amplitude and +amplitude. The seed makes it the
/// same noise every run, which is what a test needs.
[[nodiscard]] Signal whiteNoise(std::size_t frames, float amplitude = 1.0f, std::uint32_t seed = 1);

/// Noise whose power falls by 3 dB per octave, which is closer to programme
/// material than white noise is.
[[nodiscard]] Signal pinkNoise(std::size_t frames, float amplitude = 1.0f, std::uint32_t seed = 1);

/// One sample of \a amplitude at \a at, silence either side. Feed this in to
/// measure a latency or an impulse response.
[[nodiscard]] Signal impulse(std::size_t frames, std::size_t at = 0, float amplitude = 1.0f);

/// Silence until \a at, then \a amplitude for the rest.
[[nodiscard]] Signal step(std::size_t frames, std::size_t at, float amplitude = 1.0f);

/// A tone that comes and goes: \a onFrames of sine, \a offFrames of silence,
/// over and over. This is what an envelope follower or a gate is measured on.
[[nodiscard]] Signal burst(std::size_t frames,
                           double frequencyHz,
                           std::size_t onFrames,
                           std::size_t offFrames,
                           double sampleRate = kTestSampleRate,
                           float amplitude = 1.0f);

/// \a channels copies of \a signal, for a block that wants more than one.
[[nodiscard]] MultiSignal spread(const Signal &signal, std::uint32_t channels);

/// How many frames each channel of \a signal holds; zero if it has none.
[[nodiscard]] std::size_t frameCount(const MultiSignal &signal);

} // namespace rb::test

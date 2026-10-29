// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "signals.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>

namespace rb::test
{

Signal silence(std::size_t frames)
{
    return Signal(frames, 0.0f);
}

Signal constant(std::size_t frames, float value)
{
    return Signal(frames, value);
}

Signal sine(std::size_t frames, double frequencyHz, double sampleRate, float amplitude, double phase)
{
    Signal samples(frames);
    const double step = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        samples[frame] = amplitude * static_cast<float>(std::sin(phase + step * static_cast<double>(frame)));
    }
    return samples;
}

Signal sweep(std::size_t frames, double startHz, double endHz, double sampleRate, float amplitude)
{
    Signal samples(frames);
    if (frames == 0 || !(startHz > 0.0) || !(endHz > 0.0)) {
        return samples;
    }
    // Exponential in frequency, so the phase is the integral of it. Written
    // out rather than stepped, so rounding cannot drift over a long sweep.
    const double seconds = static_cast<double>(frames) / sampleRate;
    const double ratio = endHz / startHz;
    const double factor = 2.0 * std::numbers::pi * startHz * seconds / std::log(ratio);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double time = static_cast<double>(frame) / sampleRate / seconds;
        const double phase = factor * (std::pow(ratio, time) - 1.0);
        samples[frame] = amplitude * static_cast<float>(std::sin(phase));
    }
    return samples;
}

Signal whiteNoise(std::size_t frames, float amplitude, std::uint32_t seed)
{
    Signal samples(frames);
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> between(-amplitude, amplitude);
    for (float &sample : samples) {
        sample = between(random);
    }
    return samples;
}

Signal pinkNoise(std::size_t frames, float amplitude, std::uint32_t seed)
{
    // Three one-pole filters in parallel, the usual economical way to bend
    // white noise by 3 dB per octave across the audible range.
    Signal samples = whiteNoise(frames, 1.0f, seed);
    double first = 0.0;
    double second = 0.0;
    double third = 0.0;
    float loudest = 0.0f;
    for (float &sample : samples) {
        const double white = static_cast<double>(sample);
        first = 0.99765 * first + white * 0.0990460;
        second = 0.96300 * second + white * 0.2965164;
        third = 0.57000 * third + white * 1.0526913;
        const auto pink = static_cast<float>(first + second + third + white * 0.1848);
        sample = pink;
        loudest = std::max(loudest, std::abs(pink));
    }
    // Brought back to the amplitude that was asked for; the filters raise the
    // level by a factor that depends on the noise.
    if (loudest > 0.0f) {
        const float scale = amplitude / loudest;
        for (float &sample : samples) {
            sample *= scale;
        }
    }
    return samples;
}

Signal impulse(std::size_t frames, std::size_t at, float amplitude)
{
    Signal samples(frames, 0.0f);
    if (at < frames) {
        samples[at] = amplitude;
    }
    return samples;
}

Signal step(std::size_t frames, std::size_t at, float amplitude)
{
    Signal samples(frames, 0.0f);
    for (std::size_t frame = at; frame < frames; ++frame) {
        samples[frame] = amplitude;
    }
    return samples;
}

Signal burst(std::size_t frames,
             double frequencyHz,
             std::size_t onFrames,
             std::size_t offFrames,
             double sampleRate,
             float amplitude)
{
    Signal samples = sine(frames, frequencyHz, sampleRate, amplitude);
    const std::size_t period = onFrames + offFrames;
    if (period == 0) {
        return samples;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (frame % period >= onFrames) {
            samples[frame] = 0.0f;
        }
    }
    return samples;
}

MultiSignal spread(const Signal &signal, std::uint32_t channels)
{
    return MultiSignal(channels, signal);
}

std::size_t frameCount(const MultiSignal &signal)
{
    return signal.empty() ? 0 : signal.front().size();
}

} // namespace rb::test

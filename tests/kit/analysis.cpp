// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "analysis.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::test
{

float peak(std::span<const float> signal)
{
    float loudest = 0.0f;
    for (const float sample : signal) {
        loudest = std::max(loudest, std::abs(sample));
    }
    return loudest;
}

float rms(std::span<const float> signal)
{
    if (signal.empty()) {
        return 0.0f;
    }
    double sum = 0.0;
    for (const float sample : signal) {
        sum += static_cast<double>(sample) * static_cast<double>(sample);
    }
    return static_cast<float>(std::sqrt(sum / static_cast<double>(signal.size())));
}

float peakDb(std::span<const float> signal)
{
    return dsp::gainToDb(peak(signal));
}

float rmsDb(std::span<const float> signal)
{
    return dsp::gainToDb(rms(signal));
}

float maxSampleStep(std::span<const float> signal)
{
    float largest = 0.0f;
    for (std::size_t frame = 1; frame < signal.size(); ++frame) {
        largest = std::max(largest, std::abs(signal[frame] - signal[frame - 1]));
    }
    return largest;
}

float nullTestDb(std::span<const float> a, std::span<const float> b)
{
    const std::size_t frames = std::min(a.size(), b.size());
    if (frames == 0) {
        return dsp::kSilenceDb;
    }
    float loudest = 0.0f;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        loudest = std::max(loudest, std::abs(a[frame] - b[frame]));
    }
    return dsp::gainToDb(loudest);
}

bool nulls(std::span<const float> a, std::span<const float> b, float toleranceDb)
{
    if (a.size() != b.size()) {
        return false;
    }
    return nullTestDb(a, b) <= toleranceDb;
}

std::optional<int> latencySamples(std::span<const float> reference, std::span<const float> delayed, int maxLag)
{
    if (reference.empty() || delayed.empty() || maxLag < 0) {
        return std::nullopt;
    }
    double best = 0.0;
    int bestLag = 0;
    bool found = false;
    for (int lag = 0; lag <= maxLag; ++lag) {
        double correlation = 0.0;
        for (std::size_t frame = 0; frame + static_cast<std::size_t>(lag) < delayed.size() && frame < reference.size();
             ++frame) {
            correlation += static_cast<double>(reference[frame])
                * static_cast<double>(delayed[frame + static_cast<std::size_t>(lag)]);
        }
        if (!found || correlation > best) {
            best = correlation;
            bestLag = lag;
            found = true;
        }
    }
    if (!found || best <= 0.0) {
        return std::nullopt; // nothing in either signal to line up
    }
    return bestLag;
}

float amplitudeAt(std::span<const float> signal, double frequencyHz, double sampleRate)
{
    if (signal.empty() || !(frequencyHz > 0.0) || !(sampleRate > 0.0)) {
        return 0.0f;
    }
    const double step = 2.0 * std::numbers::pi * frequencyHz / sampleRate;
    double real = 0.0;
    double imaginary = 0.0;
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        const double phase = step * static_cast<double>(frame);
        real += static_cast<double>(signal[frame]) * std::cos(phase);
        imaginary += static_cast<double>(signal[frame]) * std::sin(phase);
    }
    // Twice the average, because the energy of a real sine is split between
    // the positive and the negative frequency.
    const double frames = static_cast<double>(signal.size());
    return static_cast<float>(2.0 * std::hypot(real, imaginary) / frames);
}

float totalHarmonicDistortion(std::span<const float> signal, double frequencyHz, double sampleRate, int harmonics)
{
    const float fundamental = amplitudeAt(signal, frequencyHz, sampleRate);
    if (fundamental <= 0.0f) {
        return 0.0f;
    }
    double sum = 0.0;
    for (int harmonic = 2; harmonic <= harmonics; ++harmonic) {
        const double frequency = frequencyHz * harmonic;
        if (frequency >= sampleRate / 2.0) {
            break; // above what the rate can carry
        }
        const double amplitude = amplitudeAt(signal, frequency, sampleRate);
        sum += amplitude * amplitude;
    }
    return static_cast<float>(std::sqrt(sum) / static_cast<double>(fundamental));
}

} // namespace rb::test

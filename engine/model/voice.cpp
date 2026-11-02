// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "model/voice.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::model
{

namespace
{

constexpr std::size_t kBandWindow = 1024;
constexpr double kLowestBand = 30.0;
constexpr double kHighestBand = 8000.0;
/// What a band is held above before its logarithm.
constexpr float kFloor = 1e-5f;

/// The estimator's lowest pitch, in cents above 10 Hz, and its step.
constexpr double kLowestCents = 1997.3794084376191;
constexpr double kCentsStep = 20.0;
constexpr double kReferenceHz = 10.0;
/// Bins either side of the likeliest that its pitch is averaged over.
constexpr std::ptrdiff_t kPitchReach = 4;
/// Below this nothing is heard as a pitch.
constexpr float kLeastLikelihood = 0.03f;

/// The voice's pitch scale, and how far it reaches.
constexpr double kLowestStepHz = 50.0;
constexpr double kHighestStepHz = 1100.0;
constexpr double kHighestStep = 255.0;

[[nodiscard]] double melOf(double hertz) noexcept
{
    return 2595.0 * std::log10(1.0 + hertz / 700.0);
}

[[nodiscard]] double hertzOf(double mel) noexcept
{
    return 700.0 * (std::pow(10.0, mel / 2595.0) - 1.0);
}

[[nodiscard]] double stepScale(double hertz) noexcept
{
    return 1127.0 * std::log(1.0 + hertz / 700.0);
}

} // namespace

VoiceBands::VoiceBands()
{
    m_fft.prepare(kBandWindow);
    m_frame.resize(kBandWindow);
    m_spectrum.resize(m_fft.bins());
    m_magnitude.resize(m_fft.bins());
    m_window.resize(kBandWindow);
    for (std::size_t index = 0; index < kBandWindow; ++index) {
        m_window[index] =
            static_cast<float>(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(index) / kBandWindow));
    }

    // Triangles even on the mel scale, each scaled by its width so a band
    // measures a density and not how wide it happens to be.
    const double lowest = melOf(kLowestBand);
    const double highest = melOf(kHighestBand);
    std::vector<double> edges(kVoiceBands + 2);
    for (std::size_t edge = 0; edge < edges.size(); ++edge) {
        edges[edge] = hertzOf(lowest + (highest - lowest) * static_cast<double>(edge) / (kVoiceBands + 1));
    }
    const double binHz = static_cast<double>(kVoiceInputRate) / kBandWindow;
    m_bands.resize(kVoiceBands);
    for (std::size_t band = 0; band < kVoiceBands; ++band) {
        const double low = edges[band];
        const double middle = edges[band + 1];
        const double high = edges[band + 2];
        const double scale = 2.0 / (high - low);
        bool started = false;
        for (std::size_t bin = 0; bin < m_fft.bins(); ++bin) {
            const double hertz = static_cast<double>(bin) * binHz;
            const double rising = (hertz - low) / (middle - low);
            const double falling = (high - hertz) / (high - middle);
            const double weight = std::min(rising, falling);
            if (weight <= 0.0) {
                if (started) {
                    break;
                }
                continue;
            }
            if (!started) {
                m_bands[band].first = bin;
                started = true;
            }
            m_bands[band].weights.push_back(static_cast<float>(weight * scale));
        }
    }
}

std::size_t VoiceBands::frames(std::size_t samples) noexcept
{
    return samples / kVoiceFrame + 1;
}

void VoiceBands::analyse(std::span<const float> sound, std::span<float> bands)
{
    const auto count = static_cast<std::ptrdiff_t>(sound.size());
    const std::size_t frameCount = frames(sound.size());
    const auto half = static_cast<std::ptrdiff_t>(kBandWindow / 2);
    // Mirrored about the end samples, so a frame there has a whole window.
    const auto sample = [&sound, count](std::ptrdiff_t at) noexcept {
        if (at < 0) {
            at = -at;
        } else if (at >= count) {
            at = 2 * (count - 1) - at;
        }
        return at >= 0 && at < count ? sound[static_cast<std::size_t>(at)] : 0.0f;
    };
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const auto centre = static_cast<std::ptrdiff_t>(frame * kVoiceFrame);
        for (std::size_t index = 0; index < kBandWindow; ++index) {
            m_frame[index] = sample(centre - half + static_cast<std::ptrdiff_t>(index)) * m_window[index];
        }
        m_fft.forward(m_frame, m_spectrum);
        std::ranges::transform(m_spectrum, m_magnitude.begin(), [](std::complex<float> bin) { return std::abs(bin); });
        for (std::size_t band = 0; band < kVoiceBands; ++band) {
            const Band &weights = m_bands[band];
            float total = 0.0f;
            for (std::size_t index = 0; index < weights.weights.size(); ++index) {
                total += weights.weights[index] * m_magnitude[weights.first + index];
            }
            bands[band * frameCount + frame] = std::log(std::max(total, kFloor));
        }
    }
}

float pitchFromLikelihoods(std::span<const float> likelihoods) noexcept
{
    const auto bins = static_cast<std::ptrdiff_t>(std::min(likelihoods.size(), kVoicePitches));
    if (bins == 0) {
        return 0.0f;
    }
    std::ptrdiff_t likeliest = 0;
    for (std::ptrdiff_t bin = 1; bin < bins; ++bin) {
        if (likelihoods[static_cast<std::size_t>(bin)] > likelihoods[static_cast<std::size_t>(likeliest)]) {
            likeliest = bin;
        }
    }
    if (!(likelihoods[static_cast<std::size_t>(likeliest)] > kLeastLikelihood)) {
        return 0.0f;
    }
    double weight = 0.0;
    double cents = 0.0;
    for (std::ptrdiff_t bin = std::max<std::ptrdiff_t>(0, likeliest - kPitchReach);
         bin <= std::min(bins - 1, likeliest + kPitchReach);
         ++bin) {
        const double likelihood = likelihoods[static_cast<std::size_t>(bin)];
        weight += likelihood;
        cents += likelihood * (kLowestCents + kCentsStep * static_cast<double>(bin));
    }
    return weight > 0.0 ? static_cast<float>(kReferenceHz * std::pow(2.0, cents / weight / 1200.0)) : 0.0f;
}

float shiftedPitch(float hertz, float cents) noexcept
{
    return hertz > 0.0f ? hertz * std::pow(2.0f, cents / 1200.0f) : 0.0f;
}

std::int64_t pitchStep(float hertz) noexcept
{
    if (!(hertz > 0.0f)) {
        return 1;
    }
    const double lowest = stepScale(kLowestStepHz);
    const double highest = stepScale(kHighestStepHz);
    const double step = (stepScale(hertz) - lowest) * (kHighestStep - 1.0) / (highest - lowest) + 1.0;
    return std::llround(std::clamp(step, 1.0, kHighestStep));
}

} // namespace rb::model

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/spectrum.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace rb::dsp
{

SpectrumAnalyser::SpectrumAnalyser()
{
    prepare(m_sampleRate);
}

double SpectrumAnalyser::bandEdgeHz(std::size_t edge) noexcept
{
    const double along = static_cast<double>(edge) / static_cast<double>(kBands);
    return kLowestHz * std::pow(kHighestHz / kLowestHz, along);
}

double SpectrumAnalyser::bandCentreHz(std::size_t band) noexcept
{
    return std::sqrt(bandEdgeHz(band) * bandEdgeHz(band + 1));
}

void SpectrumAnalyser::prepare(double sampleRate)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_fft.prepare(kFftSize);
    m_window.resize(kFrameSize);
    double sumOfSquares = 0.0;
    for (std::size_t n = 0; n < kFrameSize; ++n) {
        // The periodic Hann window, which overlaps at half a frame to a
        // constant.
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / kFrameSize);
        m_window[n] = static_cast<float>(w);
        sumOfSquares += w * w;
    }
    // **Parseval, through the window**: the bins of one side of the
    // spectrum, each |X|² times this, add up to the frame's mean square.
    // A sine of amplitude A has a mean square of A²/2, so twice a band's
    // power is 1 for a full-scale sine in it: 0 dB.
    m_scale = 2.0 / (static_cast<double>(kFftSize) * sumOfSquares);
    m_history.assign(kFrameSize, 0.0f);
    // The padding stays silence: only the first kFrameSize are ever written.
    m_frame.assign(kFftSize, 0.0f);
    m_spectrum.assign(m_fft.bins(), {});
    const double framesPerSecond = m_sampleRate / static_cast<double>(kHop);
    m_blend = 1.0 - std::exp(-1.0 / (framesPerSecond * kAveragingMs * 1.0e-3));
    reset();
}

void SpectrumAnalyser::reset()
{
    std::ranges::fill(m_history, 0.0f);
    m_fresh = 0;
    m_power.fill(0.0);
    m_bandsDb.fill(kFloorDb);
    m_frames = 0;
}

void SpectrumAnalyser::push(std::span<const float> samples)
{
    for (const float sample : samples) {
        // The history is shifted a hop at a time, not a sample: at half a
        // frame a hop, that is two copies of a frame per frame worked out.
        m_history[kFrameSize - kHop + m_fresh] = sample;
        if (++m_fresh == kHop) {
            analyseFrame();
            std::copy(m_history.begin() + static_cast<std::ptrdiff_t>(kHop), m_history.end(), m_history.begin());
            m_fresh = 0;
        }
    }
}

void SpectrumAnalyser::analyseFrame()
{
    for (std::size_t n = 0; n < kFrameSize; ++n) {
        m_frame[n] = m_history[n] * m_window[n];
    }
    m_fft.forward(m_frame, m_spectrum);

    const double binWidth = m_sampleRate / static_cast<double>(kFftSize);
    const std::size_t lastBin = m_spectrum.size() - 1;
    std::array<double, kBands> power{};
    for (std::size_t band = 0; band < kBands; ++band) {
        const double low = bandEdgeHz(band);
        const double high = std::min(bandEdgeHz(band + 1), m_sampleRate / 2.0);
        if (high <= low) {
            continue;
        }
        // Every bin whose stretch — half a bin either side of it — reaches
        // into the band, taken in proportion to how much of it does.
        const auto first = static_cast<std::size_t>(std::max(0.0, std::floor(low / binWidth - 0.5)));
        const auto last = std::min(lastBin, static_cast<std::size_t>(std::ceil(high / binWidth + 0.5)));
        double sum = 0.0;
        for (std::size_t bin = first; bin <= last; ++bin) {
            const double from = (static_cast<double>(bin) - 0.5) * binWidth;
            const double to = (static_cast<double>(bin) + 0.5) * binWidth;
            const double overlap = std::min(to, high) - std::max(from, low);
            if (overlap > 0.0) {
                sum += static_cast<double>(std::norm(m_spectrum[bin])) * overlap / binWidth;
            }
        }
        power[band] = sum * m_scale;
    }

    const double blend = m_frames == 0 ? 1.0 : m_blend;
    for (std::size_t band = 0; band < kBands; ++band) {
        m_power[band] += (power[band] - m_power[band]) * blend;
        const double db = 10.0 * std::log10(std::max(2.0 * m_power[band], 1.0e-30));
        m_bandsDb[band] = static_cast<float>(std::max(db, static_cast<double>(kFloorDb)));
    }
    ++m_frames;
}

} // namespace rb::dsp

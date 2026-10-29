// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/gammatonebank.h"

#include "dsp/erbscale.h"

#include <cmath>
#include <complex>
#include <numbers>

namespace rb::dsp
{

namespace
{

constexpr int kOrder = 4;

/// Hohmann's a_γ: the ratio of a gammatone's ERB to its bandwidth parameter,
/// π (2γ−2)! 2^−(2γ−2) / ((γ−1)!)², 0.9817 for γ = 4.
constexpr double kErbPerBandwidth = std::numbers::pi * 720.0 / 64.0 / 36.0;

/// One channel's analytic response at \a omega (radians a sample): its
/// input gain over four sections of pole \a pole.
std::complex<double> analytic(std::complex<double> pole, double inputGain, double omega)
{
    const std::complex<double> one = 1.0 - pole * std::polar(1.0, -omega);
    return inputGain / (one * one * one * one);
}

/// The response of one channel's real, delayed output at \a omega, before
/// its weight: the phase factor and both halves of the analytic filter.
std::complex<double> channelResponse(std::complex<double> pole,
                                     double inputGain,
                                     std::complex<double> phase,
                                     std::size_t delay,
                                     double omega)
{
    const std::complex<double> real =
        0.5 * (phase * analytic(pole, inputGain, omega) + std::conj(phase * analytic(pole, inputGain, -omega)));
    return real * std::polar(1.0, -omega * static_cast<double>(delay));
}

} // namespace

void GammatoneBank::prepare(double sampleRate, std::size_t channels, double delayMs)
{
    m_sampleRate = sampleRate;
    m_count = channels;
    m_centres = erbCentres(channels);
    m_delay = static_cast<std::uint32_t>(std::lround(delayMs * sampleRate / 1000.0));
    // One ERB wide, never narrower than the channels are apart, so that
    // neighbouring channels always meet.
    const double widthErbs = std::max(1.0, erbSpacing(channels));

    std::vector<std::complex<double>> poles(channels);
    std::vector<double> gains(channels);
    std::vector<std::complex<double>> phases(channels);
    std::vector<std::size_t> delays(channels);
    m_poleRadius.assign(channels, 0.0);
    m_poleAngle.assign(channels, 0.0);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const double centre = m_centres[channel];
        const double bandwidth = widthErbs * erbHz(centre) / kErbPerBandwidth;
        const double radius = std::exp(-2.0 * std::numbers::pi * bandwidth / sampleRate);
        const double angle = 2.0 * std::numbers::pi * centre / sampleRate;
        poles[channel] = std::polar(radius, angle);
        m_poleRadius[channel] = radius;
        m_poleAngle[channel] = angle;
        // Two at the centre: a real sine of amplitude 1 comes out as an
        // analytic signal of magnitude 1.
        gains[channel] = 2.0 * std::pow(1.0 - radius, kOrder);

        // Where the impulse response's envelope peaks, within the common
        // delay. A channel that would peak later (only below the lowest
        // centre) is lined up at its own peak instead.
        std::complex<double> state[kOrder] = {};
        std::size_t peakAt = 0;
        double peak = 0.0;
        std::complex<double> atPeak{1.0, 0.0};
        for (std::size_t n = 0; n <= m_delay; ++n) {
            std::complex<double> value = n == 0 ? gains[channel] : 0.0;
            for (auto &stage : state) {
                stage = value + poles[channel] * stage;
                value = stage;
            }
            if (std::abs(value) > peak) {
                peak = std::abs(value);
                peakAt = n;
                atPeak = value;
            }
        }
        phases[channel] = std::conj(atPeak) / std::abs(atPeak);
        delays[channel] = m_delay - peakAt;
    }

    // Hohmann's weights: flatten the sum at every centre, many times round,
    // since each weight moves its neighbours' sums too.
    std::vector<double> weights(channels, 1.0);
    for (int round = 0; round < 100; ++round) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const double omega = 2.0 * std::numbers::pi * m_centres[channel] / sampleRate;
            std::complex<double> sum;
            for (std::size_t other = 0; other < channels; ++other) {
                sum +=
                    weights[other] * channelResponse(poles[other], gains[other], phases[other], delays[other], omega);
            }
            weights[channel] /= std::abs(sum);
        }
    }

    m_poleRe.assign(channels, 0.0f);
    m_poleIm.assign(channels, 0.0f);
    m_inputGain.assign(channels, 0.0f);
    m_phaseRe.assign(channels, 0.0f);
    m_phaseIm.assign(channels, 0.0f);
    m_weight.assign(channels, 0.0f);
    m_lineStart.assign(channels, 0);
    m_lineLength.assign(channels, 0);
    m_lineWrite.assign(channels, 0);
    std::size_t total = 0;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        m_poleRe[channel] = static_cast<float>(poles[channel].real());
        m_poleIm[channel] = static_cast<float>(poles[channel].imag());
        m_inputGain[channel] = static_cast<float>(gains[channel]);
        m_phaseRe[channel] = static_cast<float>(phases[channel].real());
        m_phaseIm[channel] = static_cast<float>(phases[channel].imag());
        m_weight[channel] = static_cast<float>(weights[channel]);
        m_lineStart[channel] = total;
        m_lineLength[channel] = delays[channel] + 1;
        total += m_lineLength[channel];
    }
    m_lines.assign(total, 0.0f);
    m_stateRe.assign(static_cast<std::size_t>(kOrder) * channels, 0.0f);
    m_stateIm.assign(static_cast<std::size_t>(kOrder) * channels, 0.0f);
    m_gain.assign(channels, 1.0f);
    m_step.assign(channels, 0.0f);
    m_target.assign(channels, 1.0f);
    m_powerSum.assign(channels, 0.0f);
    m_power.assign(channels, 0.0f);
    m_sum.assign(kControlFrames, 0.0f);
    reset();
}

double GammatoneBank::sinePower(std::size_t channel, double hz) const
{
    if (channel >= m_count) {
        return 0.0;
    }
    const double omega = 2.0 * std::numbers::pi * hz / m_sampleRate;
    const std::complex<double> pole = std::polar(m_poleRadius[channel], m_poleAngle[channel]);
    const double gain = 2.0 * std::pow(1.0 - m_poleRadius[channel], kOrder);
    // A sine of amplitude 1 is two phasors of a half; the analytic filter
    // passes the positive one and all but rejects the negative one.
    return std::norm(0.5 * analytic(pole, gain, omega));
}

void GammatoneBank::reset() noexcept RB_NONBLOCKING
{
    std::fill(m_stateRe.begin(), m_stateRe.end(), 0.0f);
    std::fill(m_stateIm.begin(), m_stateIm.end(), 0.0f);
    std::fill(m_lines.begin(), m_lines.end(), 0.0f);
    std::fill(m_lineWrite.begin(), m_lineWrite.end(), 0);
    std::fill(m_gain.begin(), m_gain.end(), 1.0f);
    std::fill(m_step.begin(), m_step.end(), 0.0f);
    std::fill(m_target.begin(), m_target.end(), 1.0f);
    std::fill(m_powerSum.begin(), m_powerSum.end(), 0.0f);
    std::fill(m_power.begin(), m_power.end(), 0.0f);
    m_position = 0;
}

void GammatoneBank::processRun(std::span<float> samples) noexcept RB_NONBLOCKING
{
    const std::size_t frames = samples.size();
    std::fill_n(m_sum.begin(), frames, 0.0f);
    const auto first = static_cast<float>(m_position);
    for (std::size_t channel = 0; channel < m_count; ++channel) {
        const float poleRe = m_poleRe[channel];
        const float poleIm = m_poleIm[channel];
        const float inputGain = m_inputGain[channel];
        const float phaseRe = m_phaseRe[channel];
        const float phaseIm = m_phaseIm[channel];
        const float weight = m_weight[channel];
        const float gain = m_gain[channel];
        const float step = m_step[channel];
        float stateRe[kOrder];
        float stateIm[kOrder];
        for (int stage = 0; stage < kOrder; ++stage) {
            stateRe[stage] = m_stateRe[static_cast<std::size_t>(stage) * m_count + channel];
            stateIm[stage] = m_stateIm[static_cast<std::size_t>(stage) * m_count + channel];
        }
        float *const line = m_lines.data() + m_lineStart[channel];
        const std::size_t length = m_lineLength[channel];
        std::size_t write = m_lineWrite[channel];
        float power = 0.0f;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            float re = inputGain * samples[frame];
            float im = 0.0f;
            for (int stage = 0; stage < kOrder; ++stage) {
                const float nextRe = re + poleRe * stateRe[stage] - poleIm * stateIm[stage];
                const float nextIm = im + poleRe * stateIm[stage] + poleIm * stateRe[stage];
                stateRe[stage] = nextRe;
                stateIm[stage] = nextIm;
                re = nextRe;
                im = nextIm;
            }
            power += re * re + im * im;
            // The real part of phase × value, into the delay line.
            line[write] = phaseRe * re - phaseIm * im;
            write = write + 1 == length ? 0 : write + 1;
            // With the line one longer than the delay, the oldest sample is
            // the one about to be written over.
            const float delayed = line[write];
            const float applied = gain + step * (first + static_cast<float>(frame) + 1.0f);
            m_sum[frame] += delayed * weight * applied;
        }
        for (int stage = 0; stage < kOrder; ++stage) {
            m_stateRe[static_cast<std::size_t>(stage) * m_count + channel] = stateRe[stage];
            m_stateIm[static_cast<std::size_t>(stage) * m_count + channel] = stateIm[stage];
        }
        m_lineWrite[channel] = write;
        m_powerSum[channel] += power;
    }
    std::copy_n(m_sum.begin(), frames, samples.begin());
    m_position += frames;
}

void GammatoneBank::finishStretch() noexcept RB_NONBLOCKING
{
    constexpr auto frames = static_cast<float>(kControlFrames);
    for (std::size_t channel = 0; channel < m_count; ++channel) {
        m_power[channel] = m_powerSum[channel] / frames;
        m_powerSum[channel] = 0.0f;
        m_gain[channel] += m_step[channel] * frames;
    }
    m_position = 0;
}

void GammatoneBank::startStretch() noexcept RB_NONBLOCKING
{
    constexpr auto frames = static_cast<float>(kControlFrames);
    for (std::size_t channel = 0; channel < m_count; ++channel) {
        m_step[channel] = (m_target[channel] - m_gain[channel]) / frames;
    }
}

} // namespace rb::dsp

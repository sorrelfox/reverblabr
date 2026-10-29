// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/dynamics.h"

#include <algorithm>
#include <cmath>
#include <span>

namespace rb::dsp
{

namespace
{

/// The one-pole coefficient that reaches 1 - 1/e of a step in \a ms.
/// **That is what the number on the control means**, so a test can measure
/// the time and get the number back.
[[nodiscard]] float coefficientFor(float ms, double sampleRate) noexcept RB_NONBLOCKING
{
    if (ms <= 0.0f) {
        return 0.0f; // at once
    }
    const double samples = static_cast<double>(ms) * 1.0e-3 * sampleRate;
    return static_cast<float>(std::exp(-1.0 / std::max(1.0, samples)));
}

} // namespace

float gainReductionDb(float inputDb, const DynamicsSettings &settings) noexcept RB_NONBLOCKING
{
    float reduction = 0.0f;

    // --- the compressor, with a quadratic soft knee ---
    const float ratio = std::max(1.0f, settings.ratio);
    const float slope = ratio >= kInfiniteRatio ? 1.0f : 1.0f - 1.0f / ratio;
    const float knee = std::max(0.0f, settings.kneeDb);
    const float over = inputDb - settings.thresholdDb;
    if (knee > 0.0f && over > -knee * 0.5f && over < knee * 0.5f) {
        // The bend: the amount taken off grows as the square of how far into
        // the knee the signal is, so the curve and its slope both join the
        // straight parts without a corner.
        const float into = over + knee * 0.5f;
        reduction = -slope * into * into / (2.0f * knee);
    } else if (over >= knee * 0.5f) {
        reduction = -slope * over;
    }

    // --- the expander, below ---
    if (settings.expanderRatio > 1.0f && inputDb < settings.expanderThresholdDb) {
        const float under = settings.expanderThresholdDb - inputDb;
        const float extra = under * (settings.expanderRatio - 1.0f);
        // **Limited by the range**, which is what makes it an expander
        // rather than a silence: a gate that shuts completely takes the room
        // with it.
        reduction -= std::min(extra, std::max(0.0f, settings.expanderRangeDb));
    }
    return reduction;
}

float outputDb(float inputDb, const DynamicsSettings &settings) noexcept RB_NONBLOCKING
{
    return inputDb + gainReductionDb(inputDb, settings) + settings.makeupDb;
}

void DynamicsProcessor::prepare(double sampleRate, std::uint32_t channels)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels = channels;
    m_truePeak.prepare(channels);
    m_makeup.prepare(m_sampleRate, kMakeupSmoothingMs);
    recomputeCoefficients();
    reset();
}

void DynamicsProcessor::setSettings(const DynamicsSettings &settings) noexcept RB_NONBLOCKING
{
    m_settings = settings;
}

void DynamicsProcessor::setTiming(const DynamicsTiming &timing) noexcept RB_NONBLOCKING
{
    if (timing == m_timing) {
        return;
    }
    m_timing = timing;
    recomputeCoefficients();
}

void DynamicsProcessor::recomputeCoefficients() noexcept RB_NONBLOCKING
{
    m_attackCoefficient = coefficientFor(m_timing.attackMs, m_sampleRate);
    m_releaseCoefficient = coefficientFor(m_timing.releaseMs, m_sampleRate);
    m_rmsCoefficient = coefficientFor(m_timing.rmsMs, m_sampleRate);
    m_holdSamples =
        static_cast<std::uint32_t>(std::max(0.0, static_cast<double>(m_timing.holdMs) * 1.0e-3 * m_sampleRate));
    m_windowSamples =
        std::max(1U, static_cast<std::uint32_t>(static_cast<double>(kPeakWindowMs) * 1.0e-3 * m_sampleRate));
}

void DynamicsProcessor::reset() noexcept RB_NONBLOCKING
{
    m_meanSquare = 0.0f;
    m_peakThisWindow = 0.0f;
    m_peakLastWindow = 0.0f;
    m_inWindow = 0;
    m_truePeak.reset();
    m_currentDb = 0.0f;
    m_hold = 0;
    m_inputDb = kSilenceDb;
    m_outputDb = kSilenceDb;
    m_reductionDb = 0.0f;
    m_makeup.reset(m_settings.makeupDb);
}

void DynamicsProcessor::process(const AudioBlock &block, std::uint32_t frames) noexcept RB_NONBLOCKING
{
    const std::uint32_t channels = std::min(m_channels, block.channelCount());
    // Set once a cycle; the glide is advanced a sample at a time below.
    m_makeup.setTarget(m_settings.makeupDb);
    float loudestInput = 0.0f;
    float loudestOutput = 0.0f;
    float deepestReduction = 0.0f;

    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        // **The louder side decides**, so one gain moves both and the image
        // cannot wander.
        float loudest = 0.0f;
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> samples = block.channel(channel);
            const float sample = frame < samples.size() ? samples[frame] : 0.0f;
            loudest = std::max(loudest, std::fabs(sample));
            m_truePeak.write(channel, sample);
        }

        // **The waveform's peak, not the samples'**: the sample now, so an
        // onset counts at once, and the points between the samples a
        // quarter of a millisecond back, which is what a tone whose samples
        // miss its top needs. Only the peak detector asks; the RMS detector
        // reads the samples, whose mean square is right whatever the phase.
        float detected = loudest;
        if (m_timing.detector == DynamicsDetector::Peak) {
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                detected = std::max(detected, m_truePeak.peak(channel));
            }
        }
        m_truePeak.advance();
        loudestInput = std::max(loudestInput, detected);

        // The largest reading of the last window or two: a steady tone gives
        // a steady level, which is what lets the sound sit on the curve.
        m_peakThisWindow = std::max(m_peakThisWindow, detected);
        if (++m_inWindow >= m_windowSamples) {
            m_peakLastWindow = m_peakThisWindow;
            m_peakThisWindow = 0.0f;
            m_inWindow = 0;
        }

        float level = std::max(m_peakThisWindow, m_peakLastWindow);
        if (m_timing.detector == DynamicsDetector::Rms) {
            // A short average, which follows what a person hears rather than
            // what the waveform happens to be doing.
            m_meanSquare = m_rmsCoefficient * m_meanSquare + (1.0f - m_rmsCoefficient) * loudest * loudest;
            // Anything this small is silence, and keeping it from going
            // denormal keeps ten minutes of quiet from costing anything.
            if (m_meanSquare < 1.0e-20f) {
                m_meanSquare = 0.0f;
            }
            level = std::sqrt(m_meanSquare);
        }

        const float levelDb = gainToDb(level);
        const float wantedDb = gainReductionDb(levelDb, m_settings);

        // **The reduction is what is smoothed**, in decibels: an attack of a
        // millisecond takes a millisecond whatever the signal is doing.
        // Going down is the attack; coming back is the release.
        const float coefficient = wantedDb < m_currentDb ? m_attackCoefficient : m_releaseCoefficient;
        m_currentDb = coefficient * m_currentDb + (1.0f - coefficient) * wantedDb;

        // The expander is held open for a while after the signal falls back,
        // or a level sitting on the threshold chatters.
        if (levelDb >= m_settings.expanderThresholdDb) {
            m_hold = m_holdSamples;
        } else if (m_hold > 0) {
            --m_hold;
            m_currentDb = std::max(m_currentDb, gainReductionDb(m_settings.expanderThresholdDb, m_settings));
        }

        const float gain = dbToGain(m_currentDb + m_makeup.next());
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> samples = block.channel(channel);
            if (frame < samples.size()) {
                samples[frame] *= gain;
                loudestOutput = std::max(loudestOutput, std::fabs(samples[frame]));
            }
        }
        deepestReduction = std::min(deepestReduction, m_currentDb);
    }

    if (frames > 0) {
        m_inputDb = gainToDb(loudestInput);
        m_outputDb = gainToDb(loudestOutput);
        m_reductionDb = deepestReduction;
    }
}

} // namespace rb::dsp

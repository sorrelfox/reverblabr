// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audioblock.h"
#include "dsp/decibel.h"
#include "dsp/smoothing.h"
#include "dsp/truepeak.h"
#include "rt/realtime.h"

#include <array>
#include <cstdint>

namespace rb::dsp
{

/// Everything the shape of a dynamics curve depends on.
///
/// **In decibels throughout**, because that is how the controls are labelled
/// and how the curve is drawn; turning them into gains is the processor's
/// business and happens once per sample, not once per control.
struct DynamicsSettings {
    // --- the compressor, above ---
    float thresholdDb = -20.0f; ///< Where it starts working.
    /// How much of what goes above the threshold comes out: 4 means 4:1.
    /// Anything at or above kInfiniteRatio is a limiter.
    float ratio = 20.0f;
    /// How wide the bend around the threshold is, in decibels. A wide knee
    /// starts working gently well before the threshold.
    float kneeDb = 12.0f;

    // --- the expander, below ---
    float expanderThresholdDb = -120.0f;
    float expanderRatio = 1.0f;
    /// The most the expander may take off, in decibels. This is what keeps
    /// a gate from being a silence.
    float expanderRangeDb = 0.0f;

    float makeupDb = 0.0f;

    [[nodiscard]] bool operator==(const DynamicsSettings &other) const = default;
};

/// A ratio at or above this is taken to be infinite: everything above the
/// threshold comes out at the threshold.
inline constexpr float kInfiniteRatio = 1000.0f;

/// How long a change of makeup gain takes to arrive. The rest of the curve
/// is smoothed by the attack and the release on its way into the signal;
/// the makeup is added afterwards and would otherwise step.
inline constexpr float kMakeupSmoothingMs = 20.0f;

/// **The curve, as one pure function.** What comes out for a steady input
/// that has been there long enough for the timing not to matter.
///
/// The curve editor draws this and the processor follows it, so what a
/// person sees and what they hear cannot drift apart: there is one
/// definition and both read it.
///
/// Below the expander threshold the signal is pushed further down, by no
/// more than the range; between the two thresholds nothing happens; above
/// the compressor's threshold it is held back, through a quadratic soft
/// knee that starts half a knee-width early.
[[nodiscard]] float outputDb(float inputDb, const DynamicsSettings &settings) noexcept RB_NONBLOCKING;

/// How much the curve takes off at that input, in decibels: never positive
/// before the makeup gain is added.
[[nodiscard]] float gainReductionDb(float inputDb, const DynamicsSettings &settings) noexcept RB_NONBLOCKING;

/// What the detector follows.
enum class DynamicsDetector : std::uint8_t {
    /// The waveform's peak, between the samples as well as at them, which
    /// catches everything.
    Peak,
    Rms, ///< A short average, which follows what a person hears.
};

/// How long the peak detector's sliding maximum looks back. Five
/// milliseconds is five cycles of a 1 kHz tone — long enough that the
/// largest sample of the window is the tone's peak whatever the phase, and
/// short enough that it costs the release almost nothing.
inline constexpr float kPeakWindowMs = 5.0f;

/// The timing, which is about the signal rather than the curve.
struct DynamicsTiming {
    float attackMs = 1.0f;
    float releaseMs = 50.0f;
    /// How long the expander stays open after the signal has fallen back
    /// below its threshold. Without it a signal sitting on the threshold
    /// opens and closes the gate over and over.
    float holdMs = 0.0f;
    DynamicsDetector detector = DynamicsDetector::Peak;
    /// The window the RMS detector averages over.
    float rmsMs = 10.0f;

    [[nodiscard]] bool operator==(const DynamicsTiming &other) const = default;
};

/// A feed-forward dynamics processor: one gain, worked out from the louder
/// channel and applied to both.
///
/// **The gain reduction is smoothed, not the detector.** Smoothing the level
/// going in would make the attack depend on how loud the signal is; smoothing
/// the decibels coming out gives an attack that takes the same time whatever
/// arrives, which is what the numbers on the controls promise.
///
/// **Stereo-linked**, so the image cannot move: the louder side decides and
/// both sides get the same gain.
class DynamicsProcessor
{
public:
    /// Works out the coefficients. **Not for the audio thread.**
    void prepare(double sampleRate, std::uint32_t channels);

    /// The curve and the timing may be changed between cycles; neither
    /// allocates and both take effect at the next sample.
    void setSettings(const DynamicsSettings &settings) noexcept RB_NONBLOCKING;
    void setTiming(const DynamicsTiming &timing) noexcept RB_NONBLOCKING;

    [[nodiscard]] const DynamicsSettings &settings() const noexcept RB_NONBLOCKING
    {
        return m_settings;
    }

    /// Forgets the signal: the gain goes back to nothing taken off.
    void reset() noexcept RB_NONBLOCKING;

    /// Processes one cycle in place. Real-time: bounded work, no allocation.
    void process(const AudioBlock &block, std::uint32_t frames) noexcept RB_NONBLOCKING;

    /// What the last cycle was like, for the curve editor's live marker.
    /// All three in decibels; the reduction is at or below zero.
    [[nodiscard]] float inputLevelDb() const noexcept RB_NONBLOCKING
    {
        return m_inputDb;
    }

    [[nodiscard]] float outputLevelDb() const noexcept RB_NONBLOCKING
    {
        return m_outputDb;
    }

    [[nodiscard]] float gainReduction() const noexcept RB_NONBLOCKING
    {
        return m_reductionDb;
    }

private:
    void recomputeCoefficients() noexcept RB_NONBLOCKING;

    DynamicsSettings m_settings;
    DynamicsTiming m_timing;
    double m_sampleRate = 48000.0;
    std::uint32_t m_channels = 2;

    float m_attackCoefficient = 0.0f;
    float m_releaseCoefficient = 0.0f;
    float m_rmsCoefficient = 0.0f;
    std::uint32_t m_holdSamples = 0;

    /// The detector's running mean square, when it is following one.
    float m_meanSquare = 0.0f;
    /// **The peak detector is a sliding maximum, not the sample itself.**
    /// A steady sine's samples run from nothing to its peak and back every
    /// cycle; feeding that straight to the curve would have the gain chasing
    /// the waveform, and a steady tone would come out somewhere above the
    /// curve rather than on it. Two buckets, swapped every window, give the
    /// largest sample of the last window or two for two comparisons a
    /// sample — no droop at all, and a delay of at most the window.
    float m_peakThisWindow = 0.0f;
    float m_peakLastWindow = 0.0f;
    /// **Where the waveform goes between its samples**. A tone at a
    /// quarter of the rate can put every sample at 0.707 of its peak, and a
    /// window of samples would read it 3 dB low for as long as it plays;
    /// the estimate's points between samples reach the peak. They are about
    /// the sample TruePeakEstimator::kDelay ago, so the sample itself goes
    /// into the window as well and an onset is not caught late; the points
    /// between arrive a quarter of a millisecond after it, well inside the
    /// window.
    TruePeakEstimator m_truePeak;
    std::uint32_t m_windowSamples = 0;
    std::uint32_t m_inWindow = 0;
    /// The gain reduction as it stands, in decibels: at or below zero.
    float m_currentDb = 0.0f;
    /// How many samples the expander is still held open for.
    std::uint32_t m_hold = 0;

    /// The makeup gain as it is being applied, in decibels.
    LinearSmoother m_makeup;

    float m_inputDb = kSilenceDb;
    float m_outputDb = kSilenceDb;
    float m_reductionDb = 0.0f;
};

} // namespace rb::dsp

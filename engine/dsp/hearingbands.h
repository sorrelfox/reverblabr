// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/monotonecurve.h"
#include "rt/realtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace rb::dsp
{

/// The level-dependent gain the Hearing Compensation blocks give each band
/// (ADR-0019): soft sound lifted a lot, loud sound little, and nothing
/// above a ceiling.

/// **Where the three gains apply.** A band's gain is its soft gain for sound
/// at kSoftInputDbfs, its average gain at kAverageInputDbfs and its loud gain
/// at kLoudInputDbfs, in a straight line (in dB) between them. The levels
/// are a band's own: a sine of amplitude 1 at the band's centre reads 0 dBFS.
///
/// Average sound sits where speech at an ordinary level falls in one of
/// sixteen bands of a recording mastered near -20 dBFS; the fitting rule's
/// scale puts average sound at 45 dB, so a level on that scale is
/// kListeningOffsetDb above the same level in dBFS. The block's Loudness
/// then sets the level by ear.
inline constexpr float kAverageInputDbfs = -40.0f;
inline constexpr float kLevelStepDb = 15.0f;
inline constexpr float kSoftInputDbfs = kAverageInputDbfs - kLevelStepDb;
inline constexpr float kLoudInputDbfs = kAverageInputDbfs + kLevelStepDb;
inline constexpr float kListeningAverageDb = 45.0f;
inline constexpr float kListeningOffsetDb = kListeningAverageDb - kAverageInputDbfs;

/// A level on the listening scale (the audiogram's and the ceilings') in
/// dBFS, and back.
[[nodiscard]] constexpr float listeningToDbfs(float db) noexcept RB_NONBLOCKING
{
    return db - kListeningOffsetDb;
}
[[nodiscard]] constexpr float dbfsToListening(float dbfs) noexcept RB_NONBLOCKING
{
    return dbfs + kListeningOffsetDb;
}

/// How quickly a band's gain follows its level (measured for ADR-0019).
enum class HearingSpeed : std::uint8_t {
    /// Attack 5 ms, release 70 ms: follows the syllables of speech, and
    /// brings soft consonants up.
    Syllabic = 0,
    /// Attack 50 ms, release 1.5 s: follows the scene, and keeps the shape
    /// of music.
    Slow = 1,
    /// A slow detector (325 ms, 1 s) sets the gain; a syllabic one takes
    /// over while a sound stands 8 dB or more above it, and the slow one is
    /// held still for up to 300 ms through that. A sudden loud sound is
    /// caught at once and the gain is back soon after it ends.
    Adaptive = 2,
};

/// The stable ids of the speeds, in their order. A session file holds them.
inline constexpr std::array<std::string_view, 3> kHearingSpeedIds{"syllabic", "slow", "adaptive"};

/// One band's settings.
struct HearingBand {
    float softGainDb = 0.0f;
    float averageGainDb = 0.0f;
    float loudGainDb = 0.0f;
    /// The highest level the band may put out, in dBFS on the band's own
    /// level.
    float ceilingDbfs = 60.0f;
};

/// What applies to every band.
struct HearingSettings {
    HearingSpeed speed = HearingSpeed::Adaptive;
    /// Added to every band's gain.
    float loudnessDb = 0.0f;
    /// Below this band level the lift falls away a decibel for every decibel
    /// quieter, down to none, so lifting soft sound does not lift the hiss
    /// of a recording.
    float noiseFloorDbfs = -75.0f;
    /// No band is ever given more gain than this.
    float maximumGainDb = 50.0f;
};

/// The gain for a steady sound at \a levelDbfs in a band set as \a band,
/// in dB: the three gains, then Loudness, the noise floor, the maximum gain
/// and the ceiling, in that order. What the band settles to; the detectors
/// decide how quickly.
[[nodiscard]] float
steadyGainDb(const HearingBand &band, const HearingSettings &settings, float levelDbfs) noexcept RB_NONBLOCKING;

/// Bands laid over a filterbank's channels (ADR-0019, as Hearing
/// Compensation 16 found it has to be done).
///
/// **The gain is worked out per channel, not per band.** A tone at one
/// band's centre also passes through the channels between it and the next
/// band; given a gain from the next band's level, which is quiet, they lift
/// the tone by the next band's soft gain, and a band's ceiling cannot hold a
/// loud sound back at all. Measured on the block: a tone at the average
/// level came out 2.9 dB too loud, and a tone at the loud level 4.6 dB. So:
///
/// - **Each channel's level** is read in two steps. First finely: the power
///   of the channels within kFineFlatErbs of it at full weight, falling away
///   to none at kFineEdgeErbs, scaled so that a sine at the channel's centre
///   reads its own level.
///   Then coupled: the loudest fine reading within \a couplingErbs, so that
///   every channel a tone passes through, its skirts included, sees the
///   tone's level and gives it the same gain. Worked out from the channels'
///   responses, a sine anywhere from 100 Hz to 16 kHz reads its level within
///   0.06 dB in every channel within 1.5 ERB of it; a triangle one ERB
///   either side read 0.66 dB off, and a wider flat top over-read by a
///   decibel once the loudest was taken. It also keeps the dips between
///   a vowel's formants lower than the peaks, which band coupling is for
///   (ADR-0019); the 16-band block couples over its band spacing.
/// - **Each channel's settings** are the bands', in a straight line in dB
///   over the ERB scale between the two band centres either side of it.
///
/// With as many bands as channels the settings are one to one.
class BandLayout
{
public:
    /// How far either side a channel's fine reading takes other channels at
    /// full weight, and where their weight has fallen to none.
    static constexpr double kFineFlatErbs = 1.0;
    static constexpr double kFineEdgeErbs = 2.0;

    /// Lays \a bands bands evenly on the ERB scale over channels centred at
    /// \a channelCentresHz, and each channel's level over the channels within
    /// \a couplingErbs of it. \a sinePower(channel, hz) is the filterbank's
    /// own (GammatoneBank::sinePower). Allocates.
    template <typename SinePower>
    void
    prepare(std::span<const double> channelCentresHz, std::size_t bands, double couplingErbs, SinePower &&sinePower)
    {
        layOut(channelCentresHz, bands, couplingErbs);
        const std::size_t channels = channelCentresHz.size();
        for (std::size_t channel = 0; channel < channels; ++channel) {
            double reads = 0.0;
            for (const auto &[other, weight] : m_fine[channel]) {
                reads += weight * sinePower(other, channelCentresHz[channel]);
            }
            m_levelScale[channel] = reads > 0.0 ? static_cast<float>(1.0 / reads) : 1.0f;
        }
    }

    [[nodiscard]] std::size_t bandCount() const noexcept RB_NONBLOCKING
    {
        return m_centres.size();
    }

    [[nodiscard]] std::size_t channelCount() const noexcept RB_NONBLOCKING
    {
        return m_levelScale.size();
    }

    [[nodiscard]] std::span<const double> centresHz() const noexcept
    {
        return m_centres;
    }

    /// The channel nearest a band's centre: where the band's "now" gain and
    /// level are read.
    [[nodiscard]] std::size_t channelOfBand(std::size_t band) const noexcept RB_NONBLOCKING
    {
        return band < m_bandChannel.size() ? m_bandChannel[band] : 0;
    }

    /// Each channel's level, as power, from the channels' own (\a
    /// channelPower) into \a levelPower. Real-time.
    void channelLevels(std::span<const float> channelPower, std::span<float> levelPower) noexcept RB_NONBLOCKING;

    /// Each channel's settings from the bands'. Real-time.
    void channelSettings(std::span<const HearingBand> bands,
                         std::span<HearingBand> channels) const noexcept RB_NONBLOCKING;

private:
    void layOut(std::span<const double> channelCentresHz, std::size_t bands, double couplingErbs);

    std::vector<double> m_centres;
    std::vector<std::size_t> m_bandChannel;
    /// The two bands a channel's settings come from, and how much of the upper.
    std::vector<std::size_t> m_lowerBand;
    std::vector<std::size_t> m_upperBand;
    std::vector<float> m_upperShare;
    /// For each channel, the channels its fine reading is taken over and
    /// their weights, and what scales that sum to read a sine's level.
    std::vector<std::vector<std::pair<std::size_t, float>>> m_fine;
    std::vector<float> m_levelScale;
    /// For each channel, the first and last channel whose fine reading its
    /// level may take.
    std::vector<std::size_t> m_coupledFrom;
    std::vector<std::size_t> m_coupledTo;
    /// The fine readings, between the two steps.
    std::vector<float> m_fineLevel;
};

/// The most control points a block whose bands follow curves has.
inline constexpr std::size_t kMaxHearingPoints = MonotoneCurve::kMaxPoints;

/// A control point of a block whose bands follow curves (Hearing
/// Compensation 64): where it is, and the band settings it asks for there.
struct HearingPoint {
    bool on = false;
    double hz = 1000.0;
    HearingBand settings;
};

/// **Each band's settings from control points**. Each of the four settings
/// follows its own MonotoneCurve through the points that are on, in log
/// frequency, and each band takes the curves' values at its centre: so
/// between two points a band's setting is never beyond what either asks
/// for, and a dip set with points is as deep as they say and no deeper.
/// Below the lowest point and above the highest the settings are theirs. A
/// point at the frequency of another counts once, the first of them; with
/// no point on, every band is \a fallback. Real-time.
void bandsFromPoints(std::span<const HearingPoint> points,
                     std::span<const double> centresHz,
                     const HearingBand &fallback,
                     std::span<HearingBand> bands) noexcept RB_NONBLOCKING;

/// Follows each channel's level and works out its gain, every stretch of
/// the filterbank's grid.
class BandGainControl
{
public:
    /// How long the block's gains take to come in when it starts: whatever
    /// the settings, the first sound out is never a jump to full gain.
    static constexpr double kFadeInSeconds = 1.0;
    /// How far over the slow level a sound has to be for Adaptive to take it
    /// as a burst, and how long the slow detector is held through one.
    static constexpr float kBurstDb = 8.0f;
    static constexpr double kBurstHoldSeconds = 0.3;
    /// How far below the noise floor the detectors stop: at that level no
    /// band has any lift left, so a pause is not an age of silence to climb
    /// back out of.
    static constexpr float kDetectorRangeDb = 80.0f;

    /// Sizes everything for \a count channels, updated every \a
    /// periodSeconds. Allocates.
    void prepare(std::size_t count, double periodSeconds);

    /// Back to silence, with the fade-in to do again. Real-time.
    void reset(const HearingSettings &settings) noexcept RB_NONBLOCKING;

    /// One stretch: each channel's level (\a levelPower, BandLayout::
    /// channelLevels()) and settings in, its gain in dB out in \a gainDb.
    void update(std::span<const float> levelPower,
                std::span<const HearingBand> settingsPerChannel,
                const HearingSettings &settings,
                std::span<float> gainDb) noexcept RB_NONBLOCKING;

    /// The level each channel was last taken at, in dBFS.
    [[nodiscard]] std::span<const float> levelsDbfs() const noexcept RB_NONBLOCKING
    {
        return m_levels;
    }

private:
    void setSpeed(HearingSpeed speed) noexcept RB_NONBLOCKING;

    double m_period = 0.0;
    HearingSpeed m_speed = HearingSpeed::Adaptive;
    float m_fastAttack = 0.0f;
    float m_fastRelease = 0.0f;
    float m_slowAttack = 0.0f;
    float m_slowRelease = 0.0f;
    int m_holdUpdates = 0;
    float m_fade = 0.0f;
    float m_fadeStep = 1.0f;
    std::vector<float> m_fast;
    std::vector<float> m_slow;
    std::vector<int> m_inBurst;
    std::vector<float> m_levels;
};

} // namespace rb::dsp

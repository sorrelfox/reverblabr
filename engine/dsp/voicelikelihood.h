// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/biquad.h"
#include "dsp/envelope.h"
#include "rt/realtime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace rb::dsp
{

/// A trained detector of voices, heard through this interface so that this
/// layer needs none of its libraries: the blocks layer holds the one there
/// is (blocks::makeVoiceModel(), ADR-0016), and a build without it hands
/// VoiceLikelihood nothing.
class VoiceModel
{
public:
    /// The rate it was trained at. Anywhere else it would be listening to
    /// the wrong frequencies, and nothing is resampled on the audio thread.
    static constexpr double kRate = 48000.0;
    /// The frame it takes: 10 ms at that rate.
    static constexpr std::size_t kFrame = 480;

    VoiceModel() = default;
    virtual ~VoiceModel() = default;
    VoiceModel(const VoiceModel &) = delete;
    VoiceModel &operator=(const VoiceModel &) = delete;

    /// How likely it is that \a frame holds a voice, 0 to 1. The samples are
    /// the engine's, full scale at 1.
    [[nodiscard]] virtual float hear(std::span<const float, kFrame> frame) noexcept RB_NONBLOCKING = 0;
    /// Forgets everything it has heard.
    virtual void restart() noexcept RB_NONBLOCKING = 0;
};

/// A voice told from other sound without a trained model, by two things
/// speech has: most of its energy in the band a telephone carries, and a
/// level in that band that moves a few times a second with the syllables.
///
/// **It opens on level alone,** so that the start of a word is not late: for
/// its first kSteadyMs a sound only has to stand kOpenAboveDb over the floor,
/// and after that it also has to move. A fan or a held note stops counting
/// there; music with a beat does not, and that is the price of having no
/// model. The floor falls at once to the quietest frame and climbs 1 dB a
/// second, so it finds the room's noise in the gaps between words.
///
/// **Every constant here is a first guess.** They were set on reasoning and
/// on synthetic signals, not on labelled speech.
class LevelAndRhythm
{
public:
    /// The band: where a telephone's speech lies.
    static constexpr double kBandLowHz = 300.0;
    static constexpr double kBandHighHz = 3400.0;
    /// The rate the level is looked at, and the band of its movement: 2 to 8
    /// changes a second, which is where syllables are.
    static constexpr double kFramesPerSecond = 100.0;
    static constexpr double kRhythmLowHz = 2.0;
    static constexpr double kRhythmHighHz = 8.0;
    static constexpr float kRhythmWindowMs = 400.0f;

    /// Nothing is read under this, so digital silence has a level to be.
    static constexpr float kLowestBandDb = -90.0f;
    /// Where the floor starts: over anything, so the first sound sets it.
    static constexpr float kNoFloorDb = 0.0f;
    /// 1 dB a second, in 10 ms frames.
    static constexpr float kFloorClimbDb = 0.01f;
    /// How far over the floor counts as a sound that could be a voice. The
    /// likelihood is 0 at kFadeInDb and 1 at kFadeInDb + kFadeSpanDb, so it
    /// passes 0.5 exactly here.
    static constexpr float kOpenAboveDb = 10.0f;
    static constexpr float kFadeInDb = 4.0f;
    static constexpr float kFadeSpanDb = 12.0f;
    /// Under this share of the whole sound's level, the band does not hold
    /// most of it, and it is no voice.
    static constexpr float kLeastBandShare = 0.5f;
    /// Open for this long, a sound also has to move by kLeastSwingDb.
    static constexpr std::uint32_t kSteadyFrames = 30; ///< 300 ms.
    static constexpr float kLeastSwingDb = 4.0f;

    /// Sizes the windows. Allocates: call it from prepare().
    void prepare(double sampleRate);
    /// Forgets everything, as after a long digital silence.
    void reset() noexcept RB_NONBLOCKING;

    /// Takes one sample of the mono feed.
    void push(float mono) noexcept RB_NONBLOCKING
    {
        const float speech = m_lowPass.process(m_highPass.process(mono, m_highCoefficients), m_lowCoefficients);
        m_speechLevel.push(speech);
        m_wholeLevel.push(mono);
    }

    /// Ends a 10 ms frame, and says how likely a voice was in it, 0 to 1.
    [[nodiscard]] float closeFrame() noexcept RB_NONBLOCKING;

private:
    Svf m_highPass;
    Svf m_lowPass;
    SvfCoefficients m_highCoefficients;
    SvfCoefficients m_lowCoefficients;
    Svf m_rhythmHighPass;
    Svf m_rhythmLowPass;
    SvfCoefficients m_rhythmHighCoefficients;
    SvfCoefficients m_rhythmLowCoefficients;
    RmsFollower m_speechLevel;
    RmsFollower m_wholeLevel;
    RmsFollower m_swing;
    float m_floorDb = kNoFloorDb;
    float m_lastBandDb = kLowestBandDb;
    float m_earlierBandDb = kLowestBandDb;
    std::uint32_t m_lastedFrames = 0;
};

/// Which detector is listening.
enum class VoiceDetector : std::uint8_t {
    /// The trained model: VoiceModel.
    Model,
    /// LevelAndRhythm, which is always there.
    LevelAndRhythm,
};

/// Whether the trained model can run here, and if not, why not. It is apart
/// from the choice of detector because a card shows it either way: the
/// model's choice stays greyed, with this reason, whichever is picked.
enum class VoiceModelState : std::uint8_t {
    Ready,
    /// The build has no model.
    NotInThisBuild,
    /// The graph runs at another rate than VoiceModel::kRate.
    NeedsModelRate,
};

/// How likely it is, 0 to 1 every 10 ms, that a mono feed holds a voice.
///
/// **One scale from two detectors**, so that what reads it does not care
/// which one ran: VoiceModel where the build has one and the rate suits it,
/// LevelAndRhythm otherwise, or when asked for by name, since it costs far
/// less. Only one of the two runs at a time.
///
/// **Frames counted in samples,** so the values are the same at every
/// quantum. A change of detector takes effect where a frame ends, and what
/// comes in starts clean.
class VoiceLikelihood
{
public:
    static constexpr float kFrameMs = 10.0f;
    /// From here on a frame counts as voiced: the value the model's own
    /// gate is built around, and where LevelAndRhythm says kOpenAboveDb.
    static constexpr float kVoicedAt = 0.5f;

    /// Makes the state for \a sampleRate. \a model is the trained detector
    /// if this build has one, else null; it stays unused and is let go at
    /// a rate it does not suit. Allocates: call it from prepare().
    void prepare(double sampleRate, std::unique_ptr<VoiceModel> model);

    /// Forgets everything, and takes up the detector asked for.
    void reset() noexcept RB_NONBLOCKING;

    /// Which detector to run when both can. Takes effect where the current
    /// frame ends.
    void setPreferred(VoiceDetector detector) noexcept RB_NONBLOCKING
    {
        m_preferred = detector;
    }

    /// Takes one sample of the mono feed. Returns true when it ended a
    /// frame, and likelihood() is new.
    bool push(float mono) noexcept RB_NONBLOCKING
    {
        if (m_detector == VoiceDetector::Model) {
            m_frame[m_fill] = mono;
        } else {
            m_levelAndRhythm.push(mono);
        }
        if (++m_fill < m_frameSamples) {
            return false;
        }
        m_fill = 0;
        closeFrame();
        return true;
    }

    /// How likely a voice was in the last whole frame, 0 to 1.
    [[nodiscard]] float likelihood() const noexcept RB_NONBLOCKING
    {
        return m_likelihood;
    }

    /// Whether the last whole frame counts as voiced.
    [[nodiscard]] bool voiced() const noexcept RB_NONBLOCKING
    {
        return m_likelihood >= kVoicedAt;
    }

    /// The detector listening now.
    [[nodiscard]] VoiceDetector detector() const noexcept RB_NONBLOCKING
    {
        return m_detector;
    }

    /// Whether the model can run here, and why not.
    [[nodiscard]] VoiceModelState modelState() const noexcept RB_NONBLOCKING
    {
        return m_modelState;
    }

    /// How long a frame is, in samples, at the prepared rate.
    [[nodiscard]] std::uint32_t frameSamples() const noexcept RB_NONBLOCKING
    {
        return m_frameSamples;
    }

private:
    [[nodiscard]] VoiceDetector detectorToRun() const noexcept RB_NONBLOCKING
    {
        return m_modelState == VoiceModelState::Ready ? m_preferred : VoiceDetector::LevelAndRhythm;
    }

    void takeUp(VoiceDetector detector) noexcept RB_NONBLOCKING;
    void closeFrame() noexcept RB_NONBLOCKING;

    std::unique_ptr<VoiceModel> m_model;
    LevelAndRhythm m_levelAndRhythm;
    std::array<float, VoiceModel::kFrame> m_frame{};
    std::uint32_t m_frameSamples = VoiceModel::kFrame;
    std::uint32_t m_fill = 0;
    float m_likelihood = 0.0f;
    VoiceDetector m_preferred = VoiceDetector::Model;
    VoiceDetector m_detector = VoiceDetector::LevelAndRhythm;
    VoiceModelState m_modelState = VoiceModelState::NotInThisBuild;
};

} // namespace rb::dsp

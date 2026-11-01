// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/envelope.h"
#include "dsp/loudness.h"
#include "dsp/paramstore.h"
#include "dsp/voicelikelihood.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Where a reading stands against the two marks a person placed.
///
/// **A word changes only 0.5 LU past a mark,** so that a reading sitting on
/// one does not flicker between two words. A new mark or a first reading has
/// no earlier word to keep, and takes the plain comparison.
class MarkPosition
{
public:
    /// Published as a whole number, so the values are the surfaces' too
    /// (VoiceGaugeWords.qml).
    enum Position : std::uint32_t {
        NoMarks = 0,
        Between,
        AboveUpper,
        BelowUpper,
        AboveLower,
        BelowLower,
    };

    static constexpr float kNotSet = -61.0f;
    static constexpr float kPastMarkLu = 0.5f;

    /// Takes the reading and the marks in use, and says where it stands.
    /// With no reading there is nothing to place, and it starts again.
    [[nodiscard]] Position update(float reading, float upper, float lower) noexcept RB_NONBLOCKING;

    void reset() noexcept RB_NONBLOCKING
    {
        m_known = false;
    }

private:
    bool m_known = false;
    bool m_overUpper = false;
    bool m_overLower = false;
    float m_upper = kNotSet;
    float m_lower = kNotSet;
};

/// Reads the level of the voice on a lane over the last few seconds of it,
/// holds it through pauses, and says where it stands against two marks.
///
/// **It reads the lane and changes nothing:** the sound comes out sample for
/// sample with no delay. The daemon draws nothing, and no sound is kept.
///
/// **The hold is no timer.** dsp::VoicedLoudness counts in seconds of voice,
/// so a pause is not counted, and the number stands still.
///
/// **It says where, never whether that is good.** The marks start Not set, no
/// figure here suggests one, and the position is a word and no colour. A
/// position is worked out here and not on a surface, so that every surface
/// says the same.
///
/// **A block can't write back to the session,** so "moving one mark past the
/// other pushes it along" is computed here for what the block uses and
/// publishes, whatever the stored values say. The card writes both values, so
/// that the session holds what is shown. After a session is loaded nothing says
/// which mark moved last, and the upper one stands.
class VoiceGauge final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "meter.voicegauge";

    /// What the block publishes (rt::BlockMeter).
    static constexpr std::size_t kStateValue = 0;
    static constexpr std::size_t kReadingValue = 1;
    static constexpr std::size_t kWholeLevelValue = 2;
    static constexpr std::size_t kMarksValue = 3;

    /// kStateValue is these fields in one whole number, each the unit of the
    /// next: under 2^24 by far, so a float holds it exactly.
    static constexpr std::uint32_t kPerVoiceNow = 10;
    static constexpr std::uint32_t kPerPosition = 100;
    static constexpr std::uint32_t kPerSecond = 1000;
    static constexpr std::uint32_t kPerDetector = 100000;
    /// kMarksValue is (upper - kNotSet) * this + (lower - kNotSet).
    static constexpr std::uint32_t kPerUpperMark = 100;

    /// The first field of kStateValue: what arrives at this point.
    enum Sound : std::uint32_t {
        NoSound = 0, ///< Under kNoSoundDb for kNoSoundMs.
        RoomOnly, ///< Sound, and no voice in it now.
        Voice, ///< A voice within the last kVoiceNowMs.
    };

    static constexpr float kNoSoundLevel = 1.0e-4f; ///< -80 dBFS.
    static constexpr std::uint32_t kNoSoundMs = 2000;
    static constexpr std::uint32_t kVoiceNowMs = 200;
    static constexpr std::uint32_t kFrameMs = 10;
    /// How much voice Use This Level needs.
    static constexpr std::uint32_t kWholeSeconds = 20;

    static constexpr float kNotSet = MarkPosition::kNotSet;
    static constexpr float kLowestMark = -60.0f;
    static constexpr float kHighestMark = -6.0f;
    /// The lower mark stays this far under the upper one.
    static constexpr float kLeastGap = 1.0f;

    enum Param : std::uint32_t {
        UpperMark = 0, ///< LUFS, or kNotSet.
        LowerMark, ///< LUFS, or kNotSet.
        RoomEachSide, ///< LU either side of a level, for Use This Level.
        ReadingSeconds, ///< Seconds of voice the reading is the mean of.
        ForgetSeconds, ///< How long without a voice before it starts clean.
        Detector, ///< model, levelAndRhythm.
        MarksPlacedOn, ///< The device's name when the marks were placed.
        ShowFloating, ///< Whether the floating window shows this block.
        ShowInTray, ///< Whether the tray shows it (app/traystate.cpp).
    };

    /// Two marks after one moved: whole numbers, in range, and at least
    /// kLeastGap apart, the one that moved standing. A mark Not set stays so
    /// and pushes nothing. The card uses the same rule (VoiceGaugeWords.qml).
    struct Marks {
        float upper = kNotSet;
        float lower = kNotSet;
    };
    [[nodiscard]] static Marks pushed(float upper, float lower, bool lowerMoved) noexcept RB_NONBLOCKING;
    /// What kMarksValue carries for \a marks.
    [[nodiscard]] static float marksWordOf(Marks marks) noexcept RB_NONBLOCKING;

    VoiceGauge();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void switchedOn() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// The level of the voice, or dsp::kSilentLufs before there is one.
    [[nodiscard]] float reading() const noexcept RB_NONBLOCKING
    {
        return m_loudness.reading();
    }

    /// The level over 20 s of voice, or dsp::kSilentLufs before that.
    [[nodiscard]] float wholeLevel() const noexcept RB_NONBLOCKING
    {
        return m_loudness.wholeLevel();
    }

    [[nodiscard]] Sound sound() const noexcept RB_NONBLOCKING
    {
        return m_sound;
    }

    [[nodiscard]] bool voiceNow() const noexcept RB_NONBLOCKING
    {
        return m_voiceNow;
    }

    [[nodiscard]] MarkPosition::Position position() const noexcept RB_NONBLOCKING
    {
        return m_positionNow;
    }

    /// Whole seconds of voice counted towards Use This Level, up to 20.
    [[nodiscard]] std::uint32_t secondsCounted() const noexcept RB_NONBLOCKING;

    /// What kStateValue, kMarksValue and SpeechNow's detector word carry now.
    [[nodiscard]] float stateWord() const noexcept RB_NONBLOCKING;
    [[nodiscard]] float marksWord() const noexcept RB_NONBLOCKING;
    [[nodiscard]] float detectorReading() const noexcept RB_NONBLOCKING;

    /// The marks the block goes by.
    [[nodiscard]] Marks marksInUse() const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    [[nodiscard]] dsp::VoiceDetector preferred() const noexcept RB_NONBLOCKING;
    void closeFrame() noexcept RB_NONBLOCKING;
    void followSettings() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    /// Which mark an edit moved last. Written by whichever thread brought the
    /// edit: the engine's for setParam(), the audio thread's for the queue.
    std::atomic<bool> m_lowerMovedLast{false};

    dsp::VoiceLikelihood m_voice;
    dsp::VoicedLoudness m_loudness;
    dsp::RmsFollower m_level;
    MarkPosition m_position;
    MarkPosition::Position m_positionNow = MarkPosition::NoMarks;
    Sound m_sound = RoomOnly;
    bool m_voiceNow = false;
    std::uint32_t m_silentMs = 0;
    std::uint32_t m_sinceVoiceMs = kVoiceNowMs;
    float m_readingSeconds = 0.0f;
    float m_forgetSeconds = 0.0f;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

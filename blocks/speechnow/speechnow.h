// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "dsp/voicelikelihood.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Says while a voice is on a lane, and for a moment after it stops.
///
/// **It reads the lane and changes nothing:** the sound comes out sample for
/// sample with no delay, and a block that changes no sound reaches no surface
/// except through what it publishes. The daemon draws nothing. The tile, the
/// tray and the floating label all read the states from the telemetry.
///
/// **A 10 ms grid,** counted in samples through dsp::VoiceLikelihood, so the
/// states come out the same at every quantum.
///
/// **"Quiet again" says what the sound did.** Nothing here knows a finished
/// talker from one drawing breath, so the word claims no more than that.
class SpeechNow final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "meter.speechnow";

    /// Three frames over the line in a row before Speaking, so that one stray
    /// frame shows nothing.
    static constexpr std::uint32_t kOpenFrames = 3;
    static constexpr std::uint32_t kFrameMs = 10;

    /// What the block publishes (rt::BlockMeter). The last slot is free.
    static constexpr std::size_t kStateValue = 0;
    static constexpr std::size_t kVoiceValue = 1;
    static constexpr std::size_t kDetectorValue = 2;

    /// kStateValue.
    static constexpr float kQuiet = 0.0f;
    static constexpr float kSpeaking = 1.0f;
    static constexpr float kQuietAgain = 2.0f;

    /// kDetectorValue: which detector runs, and why the model does not.
    static constexpr float kModelListening = 0.0f;
    static constexpr float kLevelAndRhythmChosen = 1.0f;
    static constexpr float kNoModelInThisBuild = 2.0f;
    static constexpr float kModelNeedsItsRate = 3.0f;

    enum Param : std::uint32_t {
        HoldMs = 0, ///< How long a voice may pause and still be Speaking.
        QuietAgainSeconds, ///< How long Quiet again stays. 0 turns it off.
        ShortestTalkMs, ///< Shorter than this earns no Quiet again.
        Detector, ///< model, levelAndRhythm.
        ShowFloating, ///< Whether the floating label shows this block.
        ShowInTray, ///< Whether the tray shows it (app/traystate.cpp).
    };

    SpeechNow();

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

    /// What kStateValue carries now.
    [[nodiscard]] float state() const noexcept RB_NONBLOCKING
    {
        return m_state;
    }

    /// What kDetectorValue carries now.
    [[nodiscard]] float detectorReading() const noexcept RB_NONBLOCKING;

    /// That word for a detector in that state: Voice Gauge says the same.
    [[nodiscard]] static float detectorReadingOf(dsp::VoiceModelState model,
                                                 dsp::VoiceDetector preferred) noexcept RB_NONBLOCKING;

    /// How likely a voice was in the last whole frame, 0 to 1.
    [[nodiscard]] float voice() const noexcept RB_NONBLOCKING
    {
        return m_voice.likelihood();
    }

    /// One 10 ms frame's worth of the rule. process() calls it, and a test
    /// hands it frames that no signal could time this exactly.
    void step(bool voiced) noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    [[nodiscard]] dsp::VoiceDetector preferred() const noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    dsp::VoiceLikelihood m_voice;
    float m_state = kQuiet;
    std::uint32_t m_run = 0;
    std::uint32_t m_talkMs = 0;
    std::uint32_t m_gapMs = 0;
    std::uint32_t m_markMs = 0;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

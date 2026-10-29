// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

struct DenoiseState;

namespace rb::blocks
{

/// Clears the noise from speech with nothing to learn first: RNNoise,
/// a small recurrent network trained on voices, one instance a side
/// (ADR-0016).
///
/// **RNNoise takes 480-sample frames at 48 kHz.** The block gathers each
/// side's input into a frame, runs the model once the frame is full, and
/// plays the frame that comes back during the next one: so the output does
/// not depend on how the graph cuts the input into cycles. The model's own
/// output is two frames behind its input, and the gathering adds one more:
/// **1440 samples, 30 ms, reported** and the same at every quantum.
///
/// **At another rate it passes the sound through**, still 1440 samples
/// late so nothing lined up with it moves, and publishes kWrongRate: the
/// network was trained at 48 kHz and would be listening to the wrong
/// frequencies anywhere else.
///
/// **The strength is an attenuation limit, gated by the voice.** RNNoise
/// says with every frame how likely it is that a voice is in it. While one
/// is, what the model took out is put back as far as "reduction" leaves
/// it: the output is the model's plus that share of the difference, so no
/// part of the sound is turned down further than the limit. Between words
/// the whole sound goes down to the limit, and comes back when the next
/// word starts: the model looks ahead, and the gate opens on what it sees
/// coming.
///
/// **Mono** folds both sides into one before the model and sends the one
/// result to both: one instance of the model instead of two, for a voice
/// that is mono anyway. **Switching is never a splice.** The fold glides
/// in over a frame with both models running on it, and the second stops
/// only once both have been hearing the same thing for as long as their
/// delay. Switching back starts the second model again and borrows the
/// first's output for it while it catches up, and only then does the fold
/// glide out.
class SpeechDenoise final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.speechdenoise";

    static constexpr double kModelRate = 48000.0;
    static constexpr std::size_t kFrame = 480;
    /// How far RNNoise's output is behind its input, in frames.
    static constexpr std::size_t kModelDelayFrames = 2;
    static constexpr std::uint32_t kLatency = static_cast<std::uint32_t>(kFrame * (kModelDelayFrames + 1));

    static constexpr float kDefaultReductionDb = 40.0f;
    static constexpr float kHighestReductionDb = 60.0f;
    /// How sure the model has to be that a voice is there for the gate to
    /// open, and how long it stays open after, in frames (200 ms).
    static constexpr float kVoiceThreshold = 0.5f;
    static constexpr std::size_t kHoldFrames = 20;
    /// How many frames the gate takes to close (50 ms). It opens within one.
    static constexpr std::size_t kCloseFrames = 5;
    static constexpr float kSmoothingMs = 20.0f;

    /// What the block publishes (rt::BlockMeter): its state, how likely a
    /// voice is in what it hears now (0 to 1), and how far open the gate is.
    static constexpr std::size_t kStateValue = 0;
    static constexpr std::size_t kVoiceValue = 1;
    static constexpr std::size_t kGateValue = 2;
    static constexpr float kActive = 1.0f;
    static constexpr float kWrongRate = 2.0f;

    enum Param : std::uint32_t {
        Reduction = 0, ///< dB: the most anything is turned down.
        Mono, ///< Both sides as one, through one instance of the model.
    };

    SpeechDenoise();
    ~SpeechDenoise() override;
    SpeechDenoise(const SpeechDenoise &) = delete;
    SpeechDenoise &operator=(const SpeechDenoise &) = delete;

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
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// Whether it is running the model: prepared at 48 kHz.
    [[nodiscard]] bool active() const noexcept RB_NONBLOCKING
    {
        return m_active;
    }

    /// How far open the voice gate is, 0 to 1.
    [[nodiscard]] float gate() const noexcept RB_NONBLOCKING
    {
        return m_gate;
    }

private:
    static constexpr std::size_t kChannels = 2;

    struct StateDeleter {
        void operator()(DenoiseState *state) const noexcept;
    };

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// One frame of both sides: the model, the gate and the limit.
    void processFrame() noexcept RB_NONBLOCKING;
    /// Moves the fold on at the end of a frame, taking a change of "mono".
    void advanceFold() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;
    bool m_active = false;
    /// Where the fold is for the frame being gathered: apart; gliding
    /// together (both models); together but the second model still
    /// running until it has heard as much as the first; folded (one
    /// model); the second model started again and catching up; gliding
    /// apart.
    enum class Fold : std::uint8_t {
        Apart,
        Folding,
        Settling,
        Folded,
        Warming,
        Unfolding
    };
    Fold m_fold = Fold::Apart;
    std::size_t m_foldFramesLeft = 0;
    /// How much of each side is the fold, at the start and the end of the
    /// frame being gathered.
    float m_foldFrom = 0.0f;
    float m_foldTo = 0.0f;

    std::array<std::unique_ptr<DenoiseState, StateDeleter>, kChannels> m_states;
    /// Per side: the frame being gathered, in RNNoise's scale (a 16-bit
    /// sample's); the last kModelDelayFrames + 1 frames of input, newest
    /// last, to put back what the model took out; what the model made of
    /// the last frame; and the frame being played.
    std::array<std::vector<float>, kChannels> m_gathering;
    std::array<std::vector<float>, kChannels> m_dry;
    std::array<std::vector<float>, kChannels> m_wet;
    std::array<std::vector<float>, kChannels> m_ready;
    std::size_t m_fill = 0;

    /// The model's voice probability for the last frames, newest last.
    std::array<float, kModelDelayFrames + 1> m_voice{};
    std::size_t m_holdLeft = 0;
    float m_gate = 0.0f;
    /// The limit as a gain, and where it is gliding to.
    float m_floor = 0.01f;
};

} // namespace rb::blocks

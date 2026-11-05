// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "blocks/modelblock/modelhostlink.h"
#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "model/description.h"
#include "model/shared.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// **Sound through a trained model** (ADR-0031). A folder holds the model; a
/// process of its own, reverblabr-model-host, runs it; this block only
/// copies samples to and from rings in shared memory. So however slow the
/// model is, or however it ends, this cycle runs on time and nothing else in
/// the session notices.
///
/// **What leaves this block has been through the model, or nothing does.**
/// There is no dry path. With no model, while one loads, when it fails, when
/// it is late, and when the block is switched off (BlockInfo::silentWhenOff),
/// the output is silence, and never the input. A short fade down, on sound
/// the model already made, keeps a miss from clicking. It is a rule of the
/// block, not a state of it: the only samples written to an output are the
/// rings', times a gate.
///
/// **Output frame n is the answer to input frame n.** The block's latency
/// is fixed at prepare(), from what `--inspect` says of the folder, so it
/// holds at every quantum: one hop, what the path delays, and the spare time.
/// A cycle is healthy when the rings hold all of it and kGuardMs more; the
/// guard is what the fade down is made from.
///
/// Inspecting a folder runs the host for a moment on the engine thread, in
/// setParam() and loadState(). preparedShape() changes with what it found, so
/// a new folder is a fresh block in a new graph (ADR-0022).
class ModelBlock final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.model";

    /// The fade that keeps a change from clicking, and the sound kept in
    /// hand for it.
    static constexpr double kGuardMs = 5.0;
    /// How long a miss stays on the card, so it can be read.
    static constexpr double kShownSeconds = 2.0;
    /// The loudest sample let out: +12 dB, a guard against a model gone wild.
    static constexpr float kHighestSample = 4.0f;
    /// How far a voice can be moved, either way.
    static constexpr int kMostSemitones = 24;

    /// What the block publishes (rt::BlockMeter).
    static constexpr std::size_t kStateValue = 0;
    static constexpr std::size_t kLatencyMsValue = 1;
    /// A run's time, as a percentage of the sound it makes.
    static constexpr std::size_t kLoadValue = 2;
    /// A model::Refusal while Failed or CannotKeepUp, a model::Device while
    /// Running.
    static constexpr std::size_t kDetailValue = 3;

    /// The states, as kStateValue publishes them.
    enum State : std::uint8_t {
        NoModel = 0,
        Loading = 1,
        Running = 2,
        CannotKeepUp = 3,
        Failed = 4,
        Restarting = 5,
        BadOutput = 6,
    };

    enum Param : std::uint32_t {
        Folder = 0, ///< The folder that holds the model.
        Spare, ///< How much time a run is given, on top of a hop.
        Mono, ///< What a mono model does with a stereo lane.
        Retry, ///< A press: start the host again after a failure.
        Semitones, ///< How far a voice is moved, read by the families that have a pitch.
    };

    ModelBlock();
    ~ModelBlock() override;
    ModelBlock(const ModelBlock &) = delete;
    ModelBlock &operator=(const ModelBlock &) = delete;

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
    [[nodiscard]] std::uint32_t preparedShape() const override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// What kStateValue says now, and what kDetailValue does.
    [[nodiscard]] State state() const noexcept RB_NONBLOCKING;
    [[nodiscard]] std::uint32_t detail() const noexcept RB_NONBLOCKING;
    /// The frame the rings hold the model's answer up to, for a test that has
    /// to wait for it. Zero with no host.
    [[nodiscard]] std::uint64_t wetEnd() const noexcept;
    /// How many frames this block has taken in.
    [[nodiscard]] std::uint64_t framesWritten() const noexcept
    {
        return m_written;
    }

    /// The host's process id, or -1: for a test.
    [[nodiscard]] int hostPid() const noexcept;
    /// Whether the host has its model loaded.
    [[nodiscard]] bool hostReady() const noexcept;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// Asks the host what the folder is.
    void inspect();
    [[nodiscard]] std::uint32_t spareFrames(double sampleRate) const noexcept;
    /// Lets go of the host and the rings. Nothing is running the block.
    void letGo() noexcept;
    void publish() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;
    double m_sampleRate = 48000.0;
    bool m_prepared = false;

    /// What the folder holds, as far as anybody has asked.
    model::Description m_description;
    std::string m_inspected;
    /// The two facts of it the audio thread reads, written beside it.
    std::atomic<bool> m_hasFolder{false};
    std::atomic<model::Refusal> m_refusal{model::Refusal::None};

    std::optional<model::SharedRings> m_rings;
    std::unique_ptr<ModelHostLink> m_link;

    // --- fixed at prepare(), then read on the audio thread ---
    std::array<std::span<float>, model::kRingChannels> m_inputRing;
    std::array<std::span<float>, model::kRingChannels> m_outputRing;
    std::uint64_t m_mask = 0;
    std::uint32_t m_latency = 0;
    std::uint32_t m_guard = 0;
    std::uint32_t m_longestCycle = 0;
    std::uint64_t m_shownFrames = 0;
    /// Set while the folder cannot run at this rate.
    model::Refusal m_prepareRefusal = model::Refusal::None;

    // --- the audio thread's side ---

    /// Frames taken in, which is also the number the next one gets.
    std::uint64_t m_written = 0;
    /// Wet sound before this frame is out of date: the path was cut.
    std::uint64_t m_floor = 0;
    /// What the output is multiplied by: 0 is silence.
    float m_gate = 0.0f;
    float m_gateStep = 0.0f;
    bool m_healthy = false;
    /// Whether this host's sound is due: its first answers have come round.
    bool m_delivered = false;
    bool m_cycleTooLong = false;
    /// The frame of the last miss and of the last bad output, plus one.
    std::uint64_t m_lastMiss = 0;
    std::uint64_t m_lastBad = 0;
    std::uint64_t m_badRunsSeen = 0;
    std::uint32_t m_retriesSeen = 0;
};

} // namespace rb::blocks

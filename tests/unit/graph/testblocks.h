// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/blockinfo.h"
#include "dsp/delayline.h"
#include "dsp/paramdescriptor.h"
#include "dsp/paramvalue.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::test
{

/// The parts of the block interface none of these test blocks care about.
///
/// They exist to be scheduled, not to make interesting sounds: what the
/// compiler tests need is a block that says how many ports it has, marks that
/// it ran, and puts something recognisable in its output.
class SimpleBlock : public dsp::Block
{
public:
    SimpleBlock(std::string typeId, dsp::BlockCategory category, std::uint32_t inputs, std::uint32_t outputs);

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// When in the cycle this block ran, counted across every block that
    /// shares a tick. A test uses it to check the order.
    [[nodiscard]] int lastRunAt() const noexcept
    {
        return m_lastRunAt;
    }

    [[nodiscard]] std::uint64_t cycleCount() const noexcept
    {
        return m_cycles;
    }

    /// Says the audio system fills this block's inputs, or takes its
    /// outputs, the way a capture block and an output block do.
    void setInputsFromAudioSystem(bool fromAudioSystem) noexcept
    {
        m_info.inputsFromAudioSystem = fromAudioSystem;
    }

    void setOutputsToAudioSystem(bool toAudioSystem) noexcept
    {
        m_info.outputsToAudioSystem = toAudioSystem;
    }

    void setLatencySamples(std::uint32_t samples) noexcept
    {
        m_info.latencySamples = samples;
    }

    /// The tick every block writes its place in. Reset it before a cycle.
    static int s_tick;

protected:
    void markRun() noexcept;

    dsp::BlockInfo m_info;
    int m_lastRunAt = -1;
    std::uint64_t m_cycles = 0;
};

/// Puts a fixed value in every frame of every output. A graph needs one of
/// these to be runnable at all, because the compiler insists on a source.
class ConstantSource final : public SimpleBlock
{
public:
    explicit ConstantSource(float value = 1.0f, std::uint32_t outputs = 1);

    void setValue(float value) noexcept
    {
        m_value = value;
    }

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;

private:
    float m_value = 1.0f;
};

/// Multiplies what arrives by a gain, channel for channel. An unconnected
/// input reads as silence, as the interface says it must.
class GainStage final : public SimpleBlock
{
public:
    explicit GainStage(float gain = 1.0f, std::uint32_t channels = 1);

    void setGain(float gain) noexcept
    {
        m_gain = gain;
    }

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;

private:
    float m_gain = 1.0f;
};

/// Adds up what came before it, one sample at a time, and never forgets.
///
/// This is the stand-in for an envelope or a filter's memory: the test that
/// block state survives a recompile watches this number carry on.
class RunningTotal final : public SimpleBlock
{
public:
    RunningTotal();

    [[nodiscard]] double total() const noexcept
    {
        return m_total;
    }

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;

private:
    double m_total = 0.0;
};

/// Multiplies what arrives by a gain and hands it back a fixed number of
/// samples later, reporting that delay as its latency.
///
/// This is the block a bypass has to be careful with: switch it off and the
/// signal would arrive early unless something holds it back, and a path
/// running beside it would no longer line up.
class LatentGain final : public SimpleBlock
{
public:
    explicit LatentGain(float gain = 0.5f, std::uint32_t latencySamples = 16, std::uint32_t channels = 1);

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void reset() noexcept RB_NONBLOCKING override;

private:
    dsp::DelayLine m_delay;
    float m_gain = 0.5f;
    std::uint32_t m_latency = 16;
};

/// Keeps what arrived in its last cycle, so a test can read the end of a
/// chain. Its storage is made in prepare(), so process() still allocates
/// nothing.
/// A block at one end of a lane, the way the real capture and device blocks
/// are: the audio system fills its inputs or takes its outputs, and it puts
/// what it has on the bus.
///
/// **This is the shape the bypass rule turns on**. A block with a
/// connector on both edges is passed through when it is off; one of these
/// has nothing to pass through, because what arrives at a capture block
/// comes from the audio system and what leaves a device block goes to the
/// hardware, so off means silence. `ConstantSource` cannot show that — it
/// has no inputs at all, so it was always silent when off.
class AudioSystemEnd final : public SimpleBlock
{
public:
    /// \a capturing says which end it is: true for a block the audio system
    /// fills, false for one it takes from.
    explicit AudioSystemEnd(bool capturing, std::uint32_t channels = 1);

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
};

class Sink final : public SimpleBlock
{
public:
    explicit Sink(std::uint32_t channels = 1);

    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;

    /// What channel \a channel held at the end of the last cycle.
    [[nodiscard]] std::span<const float> heard(std::uint32_t channel) const;

    [[nodiscard]] std::uint32_t framesHeard() const noexcept
    {
        return m_frames;
    }

private:
    std::vector<std::vector<float>> m_heard;
    std::uint32_t m_frames = 0;
};

} // namespace rb::test

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/blockinfo.h"
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

/// The parts of the block interface these blocks do not care about. They
/// exist to be put through the test kit and to fail it in the way it is
/// meant to catch.
class KitBlock : public dsp::Block
{
public:
    KitBlock(std::string typeId, dsp::BlockCategory category, std::uint32_t inputs, std::uint32_t outputs);

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

protected:
    dsp::BlockInfo m_info;
};

/// Passes the signal through and adds a fixed delay, so a test can measure
/// the delay back out of it.
class DelayByOneHundred final : public KitBlock
{
public:
    DelayByOneHundred();
    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;

    static constexpr std::uint32_t kDelaySamples = 100;

private:
    std::vector<float> m_line;
    std::size_t m_writeIndex = 0;
};

/// Bends the signal, so that the distortion estimate has something to find.
class SoftClipper final : public KitBlock
{
public:
    SoftClipper();
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
};

/// **Deliberately wrong.** It works out its gain once per cycle from the
/// first sample of that cycle, so the result depends on where the cycle
/// boundaries fall. This is exactly the mistake the block-size-invariance
/// check exists to catch, and there is a test that it does.
class ChunkDependent final : public KitBlock
{
public:
    ChunkDependent();
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
};

/// **Deliberately wrong.** It allocates inside process(), which the
/// allocation trap has to stop.
///
/// Not a dsp::Block, on purpose. `Block::process()` is declared
/// `RB_NONBLOCKING`, an override inherits that, and Clang refuses to compile
/// a body that allocates — which is the annotation doing its job and would
/// make this file unbuildable. So this is a plain class with the same shape:
/// code on a real-time path, which is all the trap cares about.
class AllocatesWhileProcessing
{
public:
    void process(dsp::ProcessContext &context);
};

/// A plain source for graph runs: a sine it generates itself.
class SineSource final : public KitBlock
{
public:
    explicit SineSource(double frequencyHz = 1000.0, float amplitude = 0.5f);
    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;

private:
    double m_frequencyHz = 1000.0;
    float m_amplitude = 0.5f;
    double m_sampleRate = 48000.0;
    double m_phase = 0.0;
};

/// Multiplies by a fixed gain, one sample at a time, which is what a block
/// that passes the invariance check looks like.
class FixedGain final : public KitBlock
{
public:
    explicit FixedGain(float gain = 0.5f);
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;

private:
    float m_gain = 0.5f;
};

} // namespace rb::test

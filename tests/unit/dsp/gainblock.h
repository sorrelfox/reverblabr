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
#include "dsp/paramstore.h"
#include "dsp/paramvalue.h"
#include "dsp/smoothing.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::test
{

/// The smallest block that uses every part of the SDK, for testing it.
///
/// It is not the Volume block: it lives here, in the tests, so that the
/// SDK can be exercised end to end before any real block exists, and so that
/// changing a real block cannot quietly change what these tests prove.
///
/// It has one parameter of each kind that matters: a smoothed float gain, a
/// bool mute, an enum for which channels it touches, a device reference it
/// does nothing with, and a read-only output that reports the gain it last
/// applied — which is how a real block publishes a gain reduction.
class GainBlock final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "test.gain";

    GainBlock();

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

    /// Where each parameter sits in params(), for a test that uses the
    /// real-time path.
    enum Param : std::uint32_t {
        GainDb = 0,
        Muted,
        Channels,
        Device,
        AppliedGainDb,
    };

    /// How many cycles process() has run, so a test can tell it happened.
    [[nodiscard]] std::uint64_t cycleCount() const noexcept
    {
        return m_cycles;
    }

    [[nodiscard]] bool isPrepared() const noexcept
    {
        return m_prepared;
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::BlockInfo m_info;
    dsp::ParamStore m_store;
    dsp::LinearSmoother m_gain;
    std::uint64_t m_cycles = 0;
    bool m_prepared = false;
};

} // namespace rb::test

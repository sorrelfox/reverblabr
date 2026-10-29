// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/delayline.h"
#include "dsp/ear.h"
#include "dsp/paramstore.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::test
{

/// A block for one ear, before any hearing block exists: a gain on
/// its ear, late by a latency the test chooses, through dsp::EarRouter.
///
/// **Late on purpose**, so that everything the router promises about the
/// other ear — passed through, delayed by exactly the block's latency — is
/// something a test can see.
class OneEarBlock final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "test.oneEar";
    static constexpr std::uint32_t kDefaultLatency = 64;

    enum Param : std::uint32_t {
        Ear = 0,
        GainDb,
    };

    explicit OneEarBlock(std::uint32_t latencySamples = kDefaultLatency);

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

    [[nodiscard]] const dsp::EarRouter &router() const noexcept
    {
        return m_router;
    }

    /// How many times the router has told the block to forget what it heard.
    [[nodiscard]] std::uint32_t resets() const noexcept
    {
        return m_resets;
    }

private:
    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::EarRouter m_router;
    dsp::DelayLine m_late;
    std::uint32_t m_resets = 0;
};

} // namespace rb::test

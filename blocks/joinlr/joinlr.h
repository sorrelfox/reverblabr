// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"

#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Joins two lanes into one stereo signal: the left side of one, the right
/// side of the other.
///
/// **How the two ears are put back together** when each has a chain of its
/// own. One output feeds two lanes, each lane carries one ear's blocks, and
/// Join L/R takes FL from its `left` lane and FR from its `right` lane. The
/// other two channels — the side each lane passed through untouched — are
/// dropped.
///
/// **No latency of its own.** The two chains may be late by different
/// amounts; the compiler delays the earlier lane to meet the later one where
/// they join, so the ears come out aligned without the block knowing.
class JoinLR final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "hearing.joinlr";

    JoinLR();

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

private:
    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
};

} // namespace rb::blocks

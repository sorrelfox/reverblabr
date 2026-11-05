// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/dynamics.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A compressor with one control: how firmly it should work.
///
/// **The Dynamics block underneath, with the settings chosen for you.** Each
/// mode is a threshold, a ratio, a knee, a makeup gain, an attack, a release
/// and a detector, and all of it goes through dsp::DynamicsProcessor — the
/// same curve and the same detector as the Dynamics block, so a mode here and
/// the same numbers typed into that block sound the same.
///
/// **Off is a flat curve, not a bypass.** A ratio of 1:1 and no makeup leave
/// the signal exactly as it arrived, and because the processor is still
/// running, switching from Off to a mode and back glides the way any other
/// change of mode does. A bypass would jump the makeup gain straight out of
/// the sound.
class SimpleCompressor final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.simplecompressor";

    /// One choice on the mode list: its stable id and everything it sets.
    struct Mode {
        std::string_view id;
        dsp::DynamicsSettings settings;
        dsp::DynamicsTiming timing;
    };

    /// The mode a new block starts in. **Not Off**: a compressor somebody has
    /// just put in a lane is there to compress, and Gentle is the one least
    /// likely to be heard doing it.
    static constexpr std::string_view kDefaultMode = "gentle";

    enum Param : std::uint32_t {
        ModeParam = 0,
    };

    SimpleCompressor();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// Every mode, in the order the list offers them: from nothing to the
    /// most, with Voice and Broadcast, which are for particular jobs, last.
    [[nodiscard]] static std::span<const Mode> modes() noexcept;

    /// The mode called \a id, or null if there is none.
    [[nodiscard]] static const Mode *findMode(std::string_view id) noexcept;

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

    /// The mode the control is on.
    [[nodiscard]] const Mode &mode() const noexcept RB_NONBLOCKING;

    /// The curve and the timing that mode sets, which is what a test puts
    /// through dsp::outputDb() to find out what the block will do.
    [[nodiscard]] dsp::DynamicsSettings settings() const noexcept RB_NONBLOCKING
    {
        return mode().settings;
    }

    [[nodiscard]] dsp::DynamicsTiming timing() const noexcept RB_NONBLOCKING
    {
        return mode().timing;
    }

    /// How much the last cycle took off, in decibels: at or below zero.
    [[nodiscard]] float gainReduction() const noexcept RB_NONBLOCKING
    {
        return m_processor.gainReduction();
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::DynamicsProcessor m_processor;
};

} // namespace rb::blocks

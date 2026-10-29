// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/limiter.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Holds the signal below a ceiling it cannot cross.
///
/// **The limiter Volume and Auto Level end with, on its own and with its
/// settings showing** (dsp::LookaheadLimiter): it looks 1.5 ms ahead, so the
/// gain is already down when a peak arrives, and it takes both sides down
/// together, so the picture does not move.
///
/// **True peak by default.** A signal can rise between its samples higher
/// than any of them, and a converter or an encoder downstream clips what a
/// sample-peak limiter let through. In true-peak mode the limiter detects
/// four times oversampled (dsp::TruePeakEstimator), as BS.1770 measures a
/// true peak. The latency is the same in both modes — the limiter is
/// always prepared for true peaks — so switching does not move the lane.
///
/// The ceiling glides when it is moved, so dragging it does not step the
/// gain.
class Limiter final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.limiter";

    static constexpr float kDefaultCeilingDb = -1.0f;
    static constexpr float kLowestCeilingDb = -12.0f;
    static constexpr float kDefaultReleaseMs = 100.0f;
    static constexpr float kShortestReleaseMs = 10.0f;
    static constexpr float kLongestReleaseMs = 1000.0f;

    /// Which of the block's published values is which (rt::BlockMeter): how
    /// far the gain went down in the last cycle, in dB, as a positive number.
    static constexpr std::size_t kGainReductionValue = 0;

    enum Param : std::uint32_t {
        Ceiling = 0, ///< dBFS.
        Release, ///< ms.
        TruePeak, ///< On or off.
    };

    Limiter();

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

    /// How far the gain went down in the last cycle, in dB, positive.
    [[nodiscard]] float gainReductionDb() const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// Hands the limiter what the settings say, at the top of a cycle.
    void follow() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::LookaheadLimiter m_limiter;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

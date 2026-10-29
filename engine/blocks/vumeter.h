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
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A pair of needles that follow the average level the way a studio VU
/// meter's do, dropped anywhere in a lane.
///
/// **It changes nothing**, like the Peak/RMS block: the audio comes out
/// sample for sample and with no latency.
///
/// **The needle is the block's; the clip light is the graph's.** Every
/// block's output is metered where it is made, which gives the clip
/// latch and the activity flag for nothing. What this block adds is the
/// movement of a needle: the signal is rectified and put through the
/// mechanism of a VU meter, modelled as a second-order system that reaches
/// 99 % of a step in 300 ms and swings past it by about 1.25 % (the
/// standard VU characteristic, IEC 60268-17), sample by sample so it moves
/// the same whatever the quantum.
///
/// **Calibrated on a sine.** A full-wave rectified sine averages 2/π of its
/// peak, so the average is scaled by π/2: a steady sine reads its own peak
/// amplitude, and a sine whose peaks reach −18 dBFS reads 0 VU at the
/// default reference. What is published is that amplitude; which level is
/// 0 VU is the reference setting, and the interface divides by it.
class VuMeter final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "meter.vu";

    /// The needle's second-order system: the damping that gives a 1.25 %
    /// overshoot, and the natural frequency that then reaches 99 % of a
    /// step in 300 ms. Worked out from the step response of a second-order
    /// system; `VuMeterTest.ANeedleReachesNinetyNinePercentInThreeHundredMilliseconds`
    /// measures what they give.
    static constexpr double kDamping = 0.8127;
    static constexpr double kNaturalRadPerSecond = 13.512;

    /// Which of the block's published values is which (rt::BlockMeter):
    /// where each needle is, as the amplitude of a sine that would hold it
    /// there.
    static constexpr std::size_t kLevelLeftValue = 0;
    static constexpr std::size_t kLevelRightValue = 1;

    /// The stable ids of the reference levels: the level in dBFS, of a
    /// sine's peaks, that reads 0 VU.
    static constexpr std::string_view kReferenceMinus20 = "-20";
    static constexpr std::string_view kReferenceMinus18 = "-18";
    static constexpr std::string_view kReferenceMinus14 = "-14";

    enum Param : std::uint32_t {
        Reference = 0,
        ShowInTray, ///< Kept now, used when there is a tray (M4).
    };

    VuMeter();

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

    /// Where one needle is, as an amplitude. What the block publishes, and
    /// what a test reads.
    [[nodiscard]] float level(std::uint32_t channel) const noexcept RB_NONBLOCKING;

    /// The same in VU against the chosen reference: 0 when the needle is on
    /// the reference level.
    [[nodiscard]] float vu(std::uint32_t channel) const noexcept RB_NONBLOCKING;

    /// The level in dBFS that the chosen reference puts at 0 VU.
    [[nodiscard]] float referenceDb() const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    /// The needle's position and its speed, a side. **Doubles**: the step
    /// is a few hundred thousandths of a second against a system that
    /// takes a third of one, and single precision would lose the input in
    /// the rounding of the state.
    std::array<double, 2> m_position{};
    std::array<double, 2> m_velocity{};
    double m_dt = 0.0;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

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

/// A level meter that can be dropped anywhere in a lane to see what the
/// signal is doing at that point.
///
/// **It changes nothing.** The audio comes out the way it went in, sample
/// for sample and with no latency, so a meter can be put anywhere without
/// anybody having to think about what it will do to the sound.
///
/// **Peak comes from the graph; RMS is the block's own.** Every block's
/// output is metered where it is made, and that gives the peak, the
/// clip latch and the activity flag for nothing. What this block adds is the
/// calibrated part: a root mean square over a window of a fixed length,
/// which is what a bar on a meter should follow.
///
/// **Plain RMS, not AES17.** A sine whose peak is −18 dBFS reads −21.0 dB,
/// because that is what its root mean square is; the convention that adds
/// 3.01 dB so a sine reads the same both ways can come later as an option.
/// The panel says which one it is showing.
class PeakRmsMeter final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "meter.peakrms";

    /// How long the root mean square is taken over.
    static constexpr float kWindowMs = 300.0f;

    /// Which of the block's published values is which (rt::BlockMeter):
    /// the root mean square of each side, as an amplitude.
    static constexpr std::size_t kRmsLeftValue = 0;
    static constexpr std::size_t kRmsRightValue = 1;

    /// The stable ids of the peak-hold settings. The holding itself is the
    /// interface's, which is where wall-clock time lives; the block only
    /// keeps what was chosen.
    static constexpr std::string_view kHoldOff = "off";
    static constexpr std::string_view kHold1s = "1s";
    static constexpr std::string_view kHold2s = "2s";
    static constexpr std::string_view kHold5s = "5s";
    static constexpr std::string_view kHoldForever = "forever";

    enum Param : std::uint32_t {
        PeakHold = 0,
        ShowInTray, ///< Kept now, used when there is a tray (M4).
    };

    PeakRmsMeter();

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

    /// The root mean square of one side over the window, as an amplitude.
    /// What the block publishes, and what a test reads.
    [[nodiscard]] float rms(std::uint32_t channel) const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    /// A rectangular window: the squares of the last kWindowMs of samples,
    /// summed. **Rectangular and not a one-pole**, because a meter that has
    /// to read a level to a tenth of a decibel should be measuring what it
    /// says it measures rather than approaching it.
    std::vector<float> m_squares;
    std::array<double, 2> m_sum{};
    std::uint32_t m_window = 0;
    std::uint32_t m_write = 0;
    std::uint32_t m_filled = 0;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

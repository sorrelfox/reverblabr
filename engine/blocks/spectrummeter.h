// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "rt/scope.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Shows the spectrum of the sound at this point in a lane, in bars.
///
/// **It changes nothing.** The audio comes out the way it went in, sample for
/// sample with no delay, so it can be put anywhere without anybody having to
/// think about what it does to the sound.
///
/// **Every setting is the surface's**, as for a Waveform, and for the same
/// reason. The transform runs in the window, on what this block leaves in a
/// scope slot, and only while a surface draws it. So a block nobody looks at
/// costs a single read a cycle, and a block somebody does the writing of four
/// rings, with nothing on the audio thread to start again when a setting
/// changes.
class SpectrumMeter final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "meter.spectrum";

    enum Param : std::uint32_t {
        Refresh = 0, ///< hz10, hz15, hz20, hz30, hz60, hz120.
        Motion, ///< realTime, smooth.
        Bars, ///< bars16, bars32, bars64, bars96, bars128.
        Scale, ///< logarithmic, linear.
        FftSize, ///< fft1024, fft2048, fft4096, fft8192.
        Floor, ///< db60, db80, db100, db120: the bottom of the view.
        Tilt, ///< tilt0, tilt3, tilt4p5: in dB an octave.
        Peaks, ///< Marks each bar's recent peak.
        Channels, ///< mix, left, right.
        Hold, ///< Keeps the bars shown, and stops taking new ones.
        Grid, ///< Draws the lines and the labels behind the bars.
        Style, ///< solid, segmented.
    };

    SpectrumMeter();

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
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    rt::ScopeWriter m_scope;
    rt::BlockMeter *m_meter = nullptr;
    std::uint32_t m_sampleRate = 0;
};

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/delayline.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Holds a lane back by an exact number of milliseconds, to line its
/// sound up with a picture or with another path through the session.
///
/// **To the sample.** The setting is turned into samples at the running rate
/// and rounded once; nothing is interpolated, so what comes out is what
/// went in, that many samples later.
///
/// **A change is a fade, not a jump.** Moving the delay while sound plays
/// would otherwise cut from one moment of the signal to another, which is a
/// click, and sliding a single read head there would bend the pitch on the
/// way. Instead the old delay and the new one are both read for
/// kFadeMs and faded across with gains that add to one and start and end
/// flat (sin² and cos²). A change that arrives during a fade waits for it
/// to end and then fades to the latest setting, so a drag is followed
/// fade by fade and always ends where the slider stopped.
///
/// **The delay is intended, not latency.** BlockInfo::latencySamples stays
/// zero: latency compensation lines other paths up with a block's latency,
/// which would undo exactly what this block is for, and a bypassed block
/// keeps its latency, whereas switching Sync off is how its delay is taken
/// away. What it adds is reported through intendedDelaySamples() and
/// published for the interface, which says it as information.
class Sync final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.sync";

    /// The longest delay there is, in milliseconds.
    static constexpr float kMaxDelayMs = 2000.0f;
    /// How long a change of delay fades across, in milliseconds.
    static constexpr float kFadeMs = 20.0f;

    /// Which of the block's published values is which (rt::BlockMeter): the
    /// delay it is at, or moving to, in samples at the running rate.
    static constexpr std::size_t kDelaySamplesValue = 0;

    enum Param : std::uint32_t {
        Delay = 0,
    };

    Sync();

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

    /// The delay the block is at, or fading to.
    [[nodiscard]] std::uint32_t intendedDelaySamples() const noexcept RB_NONBLOCKING override;

    /// Whether a change of delay is being faded across.
    [[nodiscard]] bool fading() const noexcept RB_NONBLOCKING
    {
        return m_fadeLeft > 0;
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    /// The setting, in samples at the rate prepare() was given.
    [[nodiscard]] std::uint32_t targetSamples() const noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    dsp::DelayLine m_line;
    double m_sampleRate = 0.0;
    std::uint32_t m_maxDelay = 0;
    std::uint32_t m_fadeLength = 1;
    /// The delay being faded from and the one being faded to. The same when
    /// nothing is moving.
    std::uint32_t m_from = 0;
    std::uint32_t m_to = 0;
    /// Samples of fade left, and where the fade is as a unit vector that
    /// turns a quarter circle across it: the new head's gain is sin², the
    /// old one's cos², so they add to one and nothing asks libm per sample.
    std::uint32_t m_fadeLeft = 0;
    double m_cos = 1.0;
    double m_sin = 0.0;
    double m_stepCos = 1.0;
    double m_stepSin = 0.0;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

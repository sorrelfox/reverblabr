// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/blockregistry.h"
#include "dsp/delayline.h"
#include "dsp/paramstore.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::plugin
{

/// **A plugin the host has built in, for testing**: a gain, and as
/// much delay as it is asked for, which it reports as its latency.
///
/// The host runs third-party plugins through their own formats later (LV2,
/// CLAP, LADSPA); every one of them is a dsp::Block to the host,
/// and so is this, which is what lets the host, its protocol and the engine
/// around it be built and measured before any of those formats is.
class TestGain final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "test.gain";
    /// The same gain, taking a fifth of a second to load a state: what a
    /// plugin with a large state is like, for a test of what the engine
    /// sends while one loads.
    static constexpr std::string_view kSlowTypeId = "test.slowgain";
    static constexpr std::chrono::milliseconds kSlowLoad{200};

    enum Param : std::uint32_t {
        Gain = 0, ///< As an amplitude, 0 to 4.
    };

    explicit TestGain(std::uint32_t latencySamples = 0, std::chrono::milliseconds loadDelay = {});

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
    /// The main thread's: checks and loads what is set, and says what is.
    dsp::ParamStore m_store;
    /// **The gain as both threads see it.** A host asks for a plugin's state
    /// on its main thread while the audio thread may be applying a change
    /// from the ring — which is how every plugin format asks, and why a
    /// plugin keeps whatever the two share safe itself.
    std::atomic<float> m_gain{1.0f};
    dsp::DelayLine m_delay;
    std::chrono::milliseconds m_loadDelay{};
};

/// Makes the built-in plugin called \a id, with \a latencySamples of delay
/// where it takes one, or nothing.
[[nodiscard]] std::unique_ptr<dsp::Block> makeBuiltinPlugin(std::string_view id, std::uint32_t latencySamples);

} // namespace rb::plugin

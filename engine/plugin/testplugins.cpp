// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "plugin/testplugins.h"

#include <algorithm>
#include <string>
#include <thread>
#include <utility>

using namespace rb::dsp;

namespace rb::plugin
{

namespace
{

constexpr std::uint32_t kChannels = 2;

[[nodiscard]] std::vector<ParamDescriptor> makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    ParamDescriptor gain;
    gain.id = "gain";
    gain.type = ParamType::Float;
    gain.minimum = ParamValue::ofFloat(0.0f);
    gain.maximum = ParamValue::ofFloat(4.0f);
    gain.defaultValue = ParamValue::ofFloat(1.0f);
    descriptors.push_back(std::move(gain));
    return descriptors;
}

} // namespace

TestGain::TestGain(std::uint32_t latencySamples, std::chrono::milliseconds loadDelay)
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
    , m_loadDelay(loadDelay)
{
    m_info.typeId = std::string{loadDelay.count() > 0 ? kSlowTypeId : kTypeId};
    m_info.category = BlockCategory::Plugins;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.latencySamples = latencySamples;
}

std::expected<void, BlockError> TestGain::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_delay.prepare(kChannels, std::max<std::uint32_t>(m_info.latencySamples, 1));
    m_delay.setDelaySamples(m_info.latencySamples);
    return {};
}

void TestGain::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const float gain = m_gain.load(std::memory_order_relaxed);
    for (std::uint32_t channel = 0; channel < kChannels && channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            out[frame] = frame < in.size() ? in[frame] * gain : 0.0f;
        }
    }
    if (m_info.latencySamples > 0) {
        m_delay.process(context.output);
    }
}

void TestGain::reset() noexcept RB_NONBLOCKING
{
    m_delay.reset();
}

void TestGain::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    if (index == Gain) {
        m_gain.store(std::clamp(value, 0.0f, 4.0f), std::memory_order_relaxed);
    }
}

std::expected<void, BlockError> TestGain::setParam(std::string_view id, const ParamValue &value)
{
    auto set = m_store.set(id, value);
    m_gain.store(m_store.number(Gain), std::memory_order_relaxed);
    return set;
}

ParamValue TestGain::param(std::string_view id) const
{
    return id == "gain" ? ParamValue::ofFloat(m_gain.load(std::memory_order_relaxed)) : m_store.value(id);
}

ParamValueMap TestGain::saveState() const
{
    return ParamValueMap{{"gain", ParamValue::ofFloat(m_gain.load(std::memory_order_relaxed))}};
}

std::expected<void, BlockError> TestGain::loadState(const ParamValueMap &state)
{
    if (m_loadDelay.count() > 0) {
        std::this_thread::sleep_for(m_loadDelay);
    }
    auto loaded = m_store.load(state);
    m_gain.store(m_store.number(Gain), std::memory_order_relaxed);
    return loaded;
}

std::unique_ptr<Block> makeBuiltinPlugin(std::string_view id, std::uint32_t latencySamples)
{
    if (id == TestGain::kTypeId) {
        return std::make_unique<TestGain>(latencySamples);
    }
    if (id == TestGain::kSlowTypeId) {
        return std::make_unique<TestGain>(latencySamples, TestGain::kSlowLoad);
    }
    return nullptr;
}

} // namespace rb::plugin

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/monoenhancer.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kLeft = 0;
constexpr std::uint32_t kRight = 1;

} // namespace

std::vector<ParamDescriptor> MonoEnhancer::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor crossover;
    crossover.id = "crossover";
    crossover.type = ParamType::Float;
    crossover.minimum = ParamValue::ofFloat(kMinimumCrossover);
    crossover.maximum = ParamValue::ofFloat(kMaximumCrossover);
    crossover.defaultValue = ParamValue::ofFloat(kDefaultCrossover);
    crossover.unit = ParamUnit::Hertz;
    crossover.taper = ParamTaper::Logarithmic;
    crossover.smoothingMs = kCrossoverSmoothingMs;
    descriptors.push_back(std::move(crossover));
    return descriptors;
}

MonoEnhancer::MonoEnhancer()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // The dry signal is not delayed, only the copy that makes the width.
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> MonoEnhancer::make()
{
    return std::make_unique<MonoEnhancer>();
}

std::expected<void, BlockError> MonoEnhancer::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    const auto delaySamples =
        std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(kDelayMs * 1.0e-3 * spec.sampleRate)));
    m_delay.assign(delaySamples, 0.0f);
    m_octaves.prepare(spec.sampleRate, kCrossoverSmoothingMs);
    settle();
    clearHistory();
    m_prepared = true;
    return {};
}

void MonoEnhancer::design(float octaves) noexcept RB_NONBLOCKING
{
    m_highPass =
        designSvf({FilterShape::HighPass, std::exp2(static_cast<double>(octaves)), kButterworthQ, 0.0}, m_sampleRate);
}

void MonoEnhancer::settle() noexcept RB_NONBLOCKING
{
    m_octaves.reset(std::log2(m_store.number(Crossover)));
    design(m_octaves.current());
}

void MonoEnhancer::clearHistory() noexcept RB_NONBLOCKING
{
    m_filter.reset();
    std::ranges::fill(m_delay, 0.0f);
    m_delayIndex = 0;
}

void MonoEnhancer::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_octaves.setTarget(std::log2(m_store.number(Crossover)));

    const std::span<float> leftIn = context.input.channel(kLeft);
    const std::span<float> rightIn = context.input.channel(kRight);
    const std::span<float> leftOut = context.output.channel(kLeft);
    const std::span<float> rightOut = context.output.channel(kRight);
    const std::size_t length = m_delay.size();
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // **Designed afresh only while the crossover moves**: a tangent a
        // sample for the length of a glide, none once it has arrived.
        if (m_octaves.isSmoothing()) {
            design(m_octaves.next());
        }
        // Both inputs read before either output is written: the graph may
        // hand a block one buffer to read and to write.
        const float left = frame < leftIn.size() ? leftIn[frame] : 0.0f;
        const float right = frame < rightIn.size() ? rightIn[frame] : 0.0f;
        const float mono = 0.5f * (left + right);

        // The ring holds the high pass's output; reading before writing is
        // what makes it exactly `length` samples old.
        const float held = m_delay[m_delayIndex];
        m_delay[m_delayIndex] = m_filter.process(mono, m_highPass);
        m_delayIndex = m_delayIndex + 1 == length ? 0 : m_delayIndex + 1;

        const float width = kDepth * held;
        if (frame < leftOut.size()) {
            leftOut[frame] = mono + width;
        }
        if (frame < rightOut.size()) {
            rightOut[frame] = mono - width;
        }
    }
}

void MonoEnhancer::reset() noexcept RB_NONBLOCKING
{
    settle();
    clearHistory();
}

void MonoEnhancer::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at the top of every cycle, so a change heard mid-drag is a glide.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> MonoEnhancer::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue MonoEnhancer::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap MonoEnhancer::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> MonoEnhancer::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        settle();
    }
    return loaded;
}

} // namespace rb::blocks

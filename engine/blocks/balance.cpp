// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/balance.h"

#include <algorithm>
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

std::vector<ParamDescriptor> Balance::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor position;
    position.id = "balance";
    position.type = ParamType::Float;
    position.minimum = ParamValue::ofFloat(kFullyLeft);
    position.maximum = ParamValue::ofFloat(kFullyRight);
    position.defaultValue = ParamValue::ofFloat(0.0f);
    position.smoothingMs = kSmoothingMs;
    descriptors.push_back(std::move(position));
    return descriptors;
}

Balance::Balance()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Balance::make()
{
    return std::make_unique<Balance>();
}

StereoGains Balance::targetGains() const noexcept RB_NONBLOCKING
{
    return balanceGains(m_store.number(Position) / kFullyRight);
}

std::expected<void, BlockError> Balance::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_gains.prepare(spec.sampleRate, kSmoothingMs);
    m_gains.reset(targetGains());
    m_prepared = true;
    return {};
}

void Balance::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_gains.setTarget(targetGains());

    const std::span<float> leftIn = context.input.channel(kLeft);
    const std::span<float> rightIn = context.input.channel(kRight);
    const std::span<float> leftOut = context.output.channel(kLeft);
    const std::span<float> rightOut = context.output.channel(kRight);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const StereoGains gains = m_gains.next();
        if (frame < leftOut.size()) {
            leftOut[frame] = frame < leftIn.size() ? leftIn[frame] * gains.left : 0.0f;
        }
        if (frame < rightOut.size()) {
            rightOut[frame] = frame < rightIn.size() ? rightIn[frame] * gains.right : 0.0f;
        }
    }
}

void Balance::reset() noexcept RB_NONBLOCKING
{
    m_gains.reset(targetGains());
}

void Balance::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at the top of every cycle, so a change that arrives mid-drag is
    // heard as a glide and not a step.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Balance::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Balance::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Balance::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Balance::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        m_gains.reset(targetGains());
    }
    return loaded;
}

} // namespace rb::blocks

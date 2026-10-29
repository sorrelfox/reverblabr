// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/inputswitch.h"

#include "rt/meter.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0f;

struct Gains {
    float a = 1.0f;
    float b = 0.0f;
};

/// A quarter turn from A to B: cos² + sin² is one all the way. **Exactly
/// one and nought at the ends**, which the float cosine of a quarter turn
/// is not, so a settled switch passes its input bit for bit and costs no
/// trigonometry.
[[nodiscard]] Gains gainsAt(float position) noexcept RB_NONBLOCKING
{
    if (position <= 0.0f) {
        return {1.0f, 0.0f};
    }
    if (position >= 1.0f) {
        return {0.0f, 1.0f};
    }
    return {std::cos(position * kQuarterTurn), std::sin(position * kQuarterTurn)};
}

} // namespace

std::vector<ParamDescriptor> InputSwitch::makeDescriptors()
{
    ParamDescriptor input;
    input.id = "input";
    input.type = ParamType::Enum;
    input.choices = {"a", "b"};
    input.defaultValue = ParamValue::ofEnum("a");

    // How long the next switch takes. Hidden: it is how a Shift-click is
    // sent, not a setting anybody sets.
    ParamDescriptor crossfade;
    crossfade.id = "crossfade";
    crossfade.type = ParamType::Float;
    crossfade.minimum = ParamValue::ofFloat(0.0f);
    crossfade.maximum = ParamValue::ofFloat(10000.0f);
    crossfade.defaultValue = ParamValue::ofFloat(kFadeMs);
    crossfade.unit = ParamUnit::Milliseconds;
    crossfade.flags.hidden = true;

    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(std::move(input));
    descriptors.push_back(std::move(crossfade));
    return descriptors;
}

InputSwitch::InputSwitch()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::lanes(kLaneWidth, {"a", "b"}, {"a_FL", "a_FR", "b_FL", "b_FR"});
    m_info.outputs = PortSpec::fixed(kLaneWidth, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> InputSwitch::make()
{
    return std::make_unique<InputSwitch>();
}

std::expected<void, BlockError> InputSwitch::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_meter = spec.meter;
    reset();
    return {};
}

void InputSwitch::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t inputs = context.input.channelCount();
    const std::uint32_t outputs = std::min(context.output.channelCount(), kLaneWidth);
    const auto channel = [&](std::uint32_t index) -> std::span<float> {
        return index < inputs ? context.input.channel(index) : std::span<float>{};
    };

    Gains gains = gainsAt(m_position);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        if (m_remaining > 0) {
            --m_remaining;
            // Measured back from the target, so a long fade does not drift
            // with rounding and lands on it exactly.
            m_position = m_target - m_step * static_cast<float>(m_remaining);
            gains = gainsAt(m_position);
        }
        const float gainA = gains.a;
        const float gainB = gains.b;
        for (std::uint32_t side = 0; side < outputs; ++side) {
            const std::span<float> out = context.output.channel(side);
            if (frame >= out.size()) {
                continue;
            }
            const std::span<float> a = channel(side);
            const std::span<float> b = channel(kLaneWidth + side);
            const float fromA = frame < a.size() ? a[frame] : 0.0f;
            const float fromB = frame < b.size() ? b[frame] : 0.0f;
            out[frame] = fromA * gainA + fromB * gainB;
        }
    }

    if (m_meter != nullptr) {
        m_meter->publish(kPositionValue, m_position);
    }
}

void InputSwitch::reset() noexcept RB_NONBLOCKING
{
    m_target = m_store.number(Input) != 0.0f ? 1.0f : 0.0f;
    m_position = m_target;
    m_step = 0.0f;
    m_remaining = 0;
}

void InputSwitch::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
    if (index != Input) {
        return;
    }
    // **The time it was sent with**, which is in the store now and will not
    // be once the edit that carried it has put it back.
    const double fullFade = static_cast<double>(m_store.number(Crossfade)) * 1.0e-3 * m_sampleRate;
    m_target = m_store.number(Input) != 0.0f ? 1.0f : 0.0f;
    // **The whole way takes the whole time**; turning back part way takes
    // the part of it that is being undone.
    const double distance = std::fabs(static_cast<double>(m_target - m_position));
    m_remaining = static_cast<std::uint32_t>(std::lround(distance * fullFade));
    m_step = m_remaining > 0 ? (m_target - m_position) / static_cast<float>(m_remaining) : 0.0f;
    if (m_remaining == 0) {
        m_position = m_target;
    }
}

std::expected<void, BlockError> InputSwitch::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue InputSwitch::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap InputSwitch::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> InputSwitch::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

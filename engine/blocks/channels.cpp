// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/channels.h"

#include <algorithm>
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

using Matrix = Channels::Matrix;

// **The ids are what a session file holds**, so they are never renamed; the
// words a person reads are the interface's. The order is the one the list
// offers: leaving it alone, the arrangements, then the repairs.
constexpr std::array kActions{
    Channels::Action{"none", Matrix{1.0f, 0.0f, 0.0f, 1.0f}},
    Channels::Action{"swap", Matrix{0.0f, 1.0f, 1.0f, 0.0f}},
    Channels::Action{"mono", Matrix{0.5f, 0.5f, 0.5f, 0.5f}},
    Channels::Action{"leftToBoth", Matrix{1.0f, 0.0f, 1.0f, 0.0f}},
    Channels::Action{"rightToBoth", Matrix{0.0f, 1.0f, 0.0f, 1.0f}},
    Channels::Action{"muteLeft", Matrix{0.0f, 0.0f, 0.0f, 1.0f}},
    Channels::Action{"muteRight", Matrix{1.0f, 0.0f, 0.0f, 0.0f}},
    Channels::Action{"invertLeft", Matrix{-1.0f, 0.0f, 0.0f, 1.0f}},
    Channels::Action{"invertRight", Matrix{1.0f, 0.0f, 0.0f, -1.0f}},
    Channels::Action{"difference", Matrix{0.5f, -0.5f, 0.5f, -0.5f}},
};

} // namespace

std::span<const Channels::Action> Channels::actions() noexcept
{
    return kActions;
}

std::vector<ParamDescriptor> Channels::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor action;
    action.id = "action";
    action.type = ParamType::Enum;
    for (const Action &each : kActions) {
        action.choices.emplace_back(each.id);
    }
    action.defaultValue = ParamValue::ofEnum(std::string{kActions.front().id});
    descriptors.push_back(std::move(action));
    return descriptors;
}

Channels::Channels()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Channels::make()
{
    return std::make_unique<Channels>();
}

Matrix Channels::targetMatrix() const noexcept RB_NONBLOCKING
{
    // **The choice's number, not its id**: asking the store for the value
    // would build a string on the audio thread.
    const auto index = static_cast<std::size_t>(m_store.number(ActionParam));
    return kActions[std::min(index, kActions.size() - 1)].matrix;
}

void Channels::resetTo(const Matrix &matrix) noexcept RB_NONBLOCKING
{
    m_weights[0].reset(matrix.leftFromLeft);
    m_weights[1].reset(matrix.leftFromRight);
    m_weights[2].reset(matrix.rightFromLeft);
    m_weights[3].reset(matrix.rightFromRight);
}

std::expected<void, BlockError> Channels::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    for (LinearSmoother &weight : m_weights) {
        weight.prepare(spec.sampleRate, kCrossfadeMs);
    }
    resetTo(targetMatrix());
    m_prepared = true;
    return {};
}

void Channels::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const Matrix target = targetMatrix();
    m_weights[0].setTarget(target.leftFromLeft);
    m_weights[1].setTarget(target.leftFromRight);
    m_weights[2].setTarget(target.rightFromLeft);
    m_weights[3].setTarget(target.rightFromRight);

    const std::span<float> leftIn = context.input.channel(kLeft);
    const std::span<float> rightIn = context.input.channel(kRight);
    const std::span<float> leftOut = context.output.channel(kLeft);
    const std::span<float> rightOut = context.output.channel(kRight);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // Both inputs are read before either output is written: the graph may
        // hand a block the same buffer to read and to write.
        const float left = frame < leftIn.size() ? leftIn[frame] : 0.0f;
        const float right = frame < rightIn.size() ? rightIn[frame] : 0.0f;
        const float leftFromLeft = m_weights[0].next();
        const float leftFromRight = m_weights[1].next();
        const float rightFromLeft = m_weights[2].next();
        const float rightFromRight = m_weights[3].next();
        if (frame < leftOut.size()) {
            leftOut[frame] = leftFromLeft * left + leftFromRight * right;
        }
        if (frame < rightOut.size()) {
            rightOut[frame] = rightFromLeft * left + rightFromRight * right;
        }
    }
}

void Channels::reset() noexcept RB_NONBLOCKING
{
    resetTo(targetMatrix());
}

void Channels::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Channels::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Channels::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Channels::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Channels::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        // A session opened with an action is heard with it from the start;
        // the crossfade is for a change made while it plays.
        resetTo(targetMatrix());
    }
    return loaded;
}

} // namespace rb::blocks

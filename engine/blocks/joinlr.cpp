// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/joinlr.h"

#include <span>
#include <string>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kLaneWidth = 2;
/// The `left` lane's FL, and the `right` lane's FR: the two sides kept.
constexpr std::uint32_t kLeftLaneLeft = 0;
constexpr std::uint32_t kRightLaneRight = kLaneWidth + 1;
constexpr std::uint32_t kOutLeft = 0;
constexpr std::uint32_t kOutRight = 1;

void copyInto(std::span<float> out, std::span<const float> in, std::uint32_t frames) noexcept RB_NONBLOCKING
{
    for (std::uint32_t frame = 0; frame < frames && frame < out.size(); ++frame) {
        out[frame] = frame < in.size() ? in[frame] : 0.0f;
    }
}

} // namespace

JoinLR::JoinLR()
    : m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Hearing;
    m_info.inputs = PortSpec::lanes(kLaneWidth, {"left", "right"}, {"left_FL", "left_FR", "right_FL", "right_FR"});
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Icon;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> JoinLR::make()
{
    return std::make_unique<JoinLR>();
}

std::expected<void, BlockError> JoinLR::prepare(const PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    return {};
}

void JoinLR::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    copyInto(context.output.channel(kOutLeft), context.input.channel(kLeftLaneLeft), context.frames);
    copyInto(context.output.channel(kOutRight), context.input.channel(kRightLaneRight), context.frames);
}

void JoinLR::reset() noexcept RB_NONBLOCKING { }

void JoinLR::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> JoinLR::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue JoinLR::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap JoinLR::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> JoinLR::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

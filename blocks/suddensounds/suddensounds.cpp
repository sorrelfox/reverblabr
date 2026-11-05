// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/suddensounds/suddensounds.h"

#include "rt/meter.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

[[nodiscard]] std::vector<ParamDescriptor> makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(earParam());

    ParamDescriptor strength;
    strength.id = "strength";
    strength.type = ParamType::Enum;
    for (const std::string_view each : SuddenSounds::kStrengthIds) {
        strength.choices.emplace_back(each);
    }
    strength.defaultValue = ParamValue::ofEnum(std::string{SuddenSounds::kStrengthIds[0]});
    descriptors.push_back(std::move(strength));
    return descriptors;
}

} // namespace

SuddenSounds::SuddenSounds()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Hearing;
    // GainReach::Adds, the default, is the right word: see the class comment.
    m_info.inputs = PortSpec::fixed(2, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // The frame follows the rate: set in prepare().
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> SuddenSounds::make()
{
    return std::make_unique<SuddenSounds>();
}

std::expected<void, BlockError> SuddenSounds::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_meter = spec.meter;
    m_cut.prepare(spec.sampleRate);
    m_info.latencySamples = m_cut.latencySamples();
    m_router.prepare(spec.sampleRate, spec.maxBlockFrames, m_info.latencySamples, earFromNumber(m_store.number(Ear)));
    m_rate = static_cast<std::uint32_t>(std::lround(spec.sampleRate));
    m_hop = m_cut.hopSamples();
    m_joinedFrames = static_cast<std::uint32_t>(std::ceil(kJoinedMs * 1.0e-3 * spec.sampleRate / m_hop));
    reset();
    return {};
}

void SuddenSounds::heard(float deepestDb) noexcept RB_NONBLOCKING
{
    m_intoSecond += m_hop;
    if (m_intoSecond >= m_rate) {
        m_intoSecond -= m_rate;
        m_second = (m_second + 1) % kSeconds;
        m_count -= m_seconds[m_second];
        m_seconds[m_second] = 0;
    }

    if (deepestDb > 0.0f) {
        if (m_framesSinceCut >= m_joinedFrames) {
            ++m_seconds[m_second];
            ++m_count;
        }
        m_framesSinceCut = 0;
    } else if (m_framesSinceCut < m_joinedFrames) {
        ++m_framesSinceCut;
    }

    const auto heldSamples = static_cast<std::uint32_t>(kHeldSeconds * static_cast<float>(m_rate));
    if (deepestDb >= m_heldDb) {
        m_heldDb = deepestDb;
        m_heldLeft = heldSamples;
    } else if (m_heldLeft > m_hop) {
        m_heldLeft -= m_hop;
    } else {
        m_heldDb = deepestDb;
        m_heldLeft = heldSamples;
    }
}

void SuddenSounds::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const auto strength =
        std::clamp<std::size_t>(static_cast<std::size_t>(m_store.number(Strength)), 0, kShares.size() - 1);
    m_cut.setShare(kShares[strength]);
    m_router.setEar(earFromNumber(m_store.number(Ear)));
    m_router.process(
        context,
        [this](std::span<float> samples) noexcept RB_NONBLOCKING {
            m_cut.process(samples, [this]() noexcept RB_NONBLOCKING { heard(m_cut.deepestCutDb()); });
        },
        [this]() noexcept RB_NONBLOCKING { m_cut.reset(); });

    if (m_meter != nullptr) {
        m_meter->publish(kSoftenedValue, m_heldDb);
        m_meter->publish(kCountValue, static_cast<float>(m_count));
    }
}

void SuddenSounds::reset() noexcept RB_NONBLOCKING
{
    m_router.reset();
    m_cut.reset();
    m_seconds.fill(0);
    m_second = 0;
    m_intoSecond = 0;
    m_count = 0;
    m_framesSinceCut = m_joinedFrames;
    m_heldDb = 0.0f;
    m_heldLeft = 0;
}

void SuddenSounds::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> SuddenSounds::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue SuddenSounds::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap SuddenSounds::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> SuddenSounds::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

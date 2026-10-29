// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/autolevel.h"

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

std::vector<ParamDescriptor> AutoLevel::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor boost;
    boost.id = "boost";
    boost.type = ParamType::Bool;
    boost.defaultValue = ParamValue::ofBool(true);
    descriptors.push_back(std::move(boost));

    ParamDescriptor target;
    target.id = "target";
    target.type = ParamType::Float;
    target.minimum = ParamValue::ofFloat(kLowestTargetLufs);
    target.maximum = ParamValue::ofFloat(kHighestTargetLufs);
    target.defaultValue = ParamValue::ofFloat(kDefaultTargetLufs);
    // Loudness units are decibels of loudness; the catalogue has no unit of
    // its own for them, and the panel says "LUFS".
    target.unit = ParamUnit::Decibels;
    descriptors.push_back(std::move(target));

    ParamDescriptor maximum;
    maximum.id = "maximumBoost";
    maximum.type = ParamType::Float;
    maximum.minimum = ParamValue::ofFloat(0.0f);
    maximum.maximum = ParamValue::ofFloat(kHighestMaximumBoostDb);
    maximum.defaultValue = ParamValue::ofFloat(kDefaultMaximumBoostDb);
    maximum.unit = ParamUnit::Decibels;
    descriptors.push_back(std::move(maximum));
    return descriptors;
}

AutoLevel::AutoLevel()
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

std::unique_ptr<Block> AutoLevel::make()
{
    return std::make_unique<AutoLevel>();
}

std::expected<void, BlockError> AutoLevel::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_loudness.prepare(spec.sampleRate);
    m_limiter.prepare(spec.sampleRate, kChannels);
    // **The limiter is always in the path**, so this is a constant the
    // graph can line other paths up around.
    m_info.latencySamples = m_limiter.latencySamples();
    // Time constants, not times to arrive: e^(-1/τ) a sample.
    m_riseCoefficient = static_cast<float>(std::exp(-1.0 / (kRiseMs * 1.0e-3 * spec.sampleRate)));
    m_fallCoefficient = static_cast<float>(std::exp(-1.0 / (kFallMs * 1.0e-3 * spec.sampleRate)));
    m_meter = spec.meter;
    reset();
    return {};
}

float AutoLevel::wantedBoostDb() const noexcept RB_NONBLOCKING
{
    const float loudness = m_loudness.loudness();
    if (m_store.number(Boost) == 0.0f || loudness < kGateLufs) {
        return 0.0f;
    }
    return std::clamp(m_store.number(Target) - loudness, 0.0f, m_store.number(MaximumBoost));
}

void AutoLevel::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::span<float> leftIn = context.input.channel(kLeft);
    const std::span<float> rightIn = context.input.channel(kRight);
    const std::span<float> leftOut = context.output.channel(kLeft);
    const std::span<float> rightOut = context.output.channel(kRight);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // Both inputs read before either output is written: the graph may
        // hand a block one buffer to read and to write.
        const float left = frame < leftIn.size() ? leftIn[frame] : 0.0f;
        const float right = frame < rightIn.size() ? rightIn[frame] : 0.0f;
        if (m_loudness.push(left, right)) {
            m_wantedDb = wantedBoostDb();
        }
        // **Down quickly only when the source got loud**; a boost falling
        // because the source went under the gate, or was switched off,
        // falls at the slow rate, so a pause does not pull it down.
        const bool loudAgain =
            m_wantedDb < m_boostDb && m_loudness.loudness() >= kGateLufs && m_store.number(Boost) != 0.0f;
        const float coefficient = loudAgain ? m_fallCoefficient : m_riseCoefficient;
        m_boostDb = m_wantedDb + (m_boostDb - m_wantedDb) * coefficient;
        const float gain = std::exp2(m_boostDb * (0.05f * 3.321928f));
        if (frame < leftOut.size()) {
            leftOut[frame] = left * gain;
        }
        if (frame < rightOut.size()) {
            rightOut[frame] = right * gain;
        }
    }

    // After the boost, on what is about to be sent on.
    m_limiter.process(context.output, context.frames);

    if (m_meter != nullptr) {
        m_meter->publish(kBoostValue, m_boostDb);
        m_meter->publish(kLoudnessValue, m_loudness.loudness());
    }
}

void AutoLevel::reset() noexcept RB_NONBLOCKING
{
    m_loudness.reset();
    m_limiter.reset();
    m_boostDb = 0.0f;
    m_wantedDb = 0.0f;
}

void AutoLevel::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at every measurement, ten times a second, and glided to.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> AutoLevel::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue AutoLevel::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap AutoLevel::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> AutoLevel::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

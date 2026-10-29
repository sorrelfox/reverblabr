// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "earblock.h"

#include "dsp/decibel.h"

#include <span>
#include <string>

using namespace rb::dsp;

namespace rb::test
{

namespace
{

[[nodiscard]] std::vector<ParamDescriptor> makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(earParam());

    ParamDescriptor gain;
    gain.id = "gainDb";
    gain.type = ParamType::Float;
    gain.minimum = ParamValue::ofFloat(-48.0f);
    gain.maximum = ParamValue::ofFloat(24.0f);
    gain.defaultValue = ParamValue::ofFloat(-6.0f);
    gain.unit = ParamUnit::Decibels;
    descriptors.push_back(std::move(gain));
    return descriptors;
}

} // namespace

OneEarBlock::OneEarBlock(std::uint32_t latencySamples)
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Hearing;
    m_info.inputs = PortSpec::fixed(2, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = latencySamples;
}

std::unique_ptr<Block> OneEarBlock::make()
{
    return std::make_unique<OneEarBlock>();
}

std::expected<void, BlockError> OneEarBlock::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_late.prepare(1, m_info.latencySamples);
    m_late.setDelaySamples(m_info.latencySamples);
    m_router.prepare(spec.sampleRate, spec.maxBlockFrames, m_info.latencySamples, earFromNumber(m_store.number(Ear)));
    return {};
}

void OneEarBlock::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_router.setEar(earFromNumber(m_store.number(Ear)));
    const float gain = dbToGain(m_store.number(GainDb));
    m_router.process(
        context,
        [this, gain](std::span<float> samples) noexcept RB_NONBLOCKING {
            for (float &sample : samples) {
                sample *= gain;
            }
            m_late.processChannel(0, samples);
            m_late.endFrames(static_cast<std::uint32_t>(samples.size()));
        },
        [this]() noexcept RB_NONBLOCKING {
            m_late.reset();
            ++m_resets;
        });
}

void OneEarBlock::reset() noexcept RB_NONBLOCKING
{
    m_late.reset();
    m_router.setEar(earFromNumber(m_store.number(Ear)));
    m_router.reset();
}

void OneEarBlock::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> OneEarBlock::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue OneEarBlock::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap OneEarBlock::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> OneEarBlock::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::test

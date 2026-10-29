// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/basstreble.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

namespace rb::blocks
{

using namespace rb::dsp;

namespace
{

[[nodiscard]] ParamDescriptor toneDescriptor(std::string id)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(-BassTreble::kRangeDb);
    descriptor.maximum = ParamValue::ofFloat(BassTreble::kRangeDb);
    descriptor.defaultValue = ParamValue::ofFloat(0.0f);
    descriptor.unit = ParamUnit::Decibels;
    descriptor.taper = ParamTaper::Linear;
    descriptor.smoothingMs = BassTreble::kSmoothingMs;
    return descriptor;
}

} // namespace

std::vector<ParamDescriptor> BassTreble::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(toneDescriptor("bass"));
    descriptors.push_back(toneDescriptor("treble"));
    return descriptors;
}

BassTreble::BassTreble()
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

std::unique_ptr<Block> BassTreble::make()
{
    return std::make_unique<BassTreble>();
}

FilterSpec BassTreble::bassShelf(double gainDb) noexcept
{
    return {FilterShape::LowShelf, kBassFrequency, kShelfQ, gainDb};
}

FilterSpec BassTreble::trebleShelf(double gainDb) noexcept
{
    return {FilterShape::HighShelf, kTrebleFrequency, kShelfQ, gainDb};
}

double BassTreble::magnitudeDb(double frequency, double bassDb, double trebleDb, double sampleRate) noexcept
{
    return filterMagnitudeDb(bassShelf(bassDb), frequency, sampleRate)
        + filterMagnitudeDb(trebleShelf(trebleDb), frequency, sampleRate);
}

std::expected<void, BlockError> BassTreble::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_bassDb.prepare(spec.sampleRate, kSmoothingMs);
    m_trebleDb.prepare(spec.sampleRate, kSmoothingMs);
    settleSmoothers();
    resetFilters();
    m_prepared = true;
    return {};
}

void BassTreble::aimSmoothers() noexcept RB_NONBLOCKING
{
    m_bassDb.setTarget(m_store.number(Bass));
    m_trebleDb.setTarget(m_store.number(Treble));
}

void BassTreble::settleSmoothers() noexcept RB_NONBLOCKING
{
    m_bassDb.reset(m_store.number(Bass));
    m_trebleDb.reset(m_store.number(Treble));
    m_bass = designSvf(bassShelf(m_bassDb.current()), m_sampleRate);
    m_treble = designSvf(trebleShelf(m_trebleDb.current()), m_sampleRate);
}

void BassTreble::resetFilters() noexcept RB_NONBLOCKING
{
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        m_bassFilters[channel].reset();
        m_trebleFilters[channel].reset();
    }
}

void BassTreble::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    aimSmoothers();
    const std::uint32_t channels = std::min(context.output.channelCount(), kChannels);

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // **Worked out afresh only while a gain moves**: once it has
        // arrived the shelf holds still and costs nothing to keep.
        if (m_bassDb.isSmoothing()) {
            m_bass = designSvf(bassShelf(m_bassDb.next()), m_sampleRate);
        }
        if (m_trebleDb.isSmoothing()) {
            m_treble = designSvf(trebleShelf(m_trebleDb.next()), m_sampleRate);
        }
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> out = context.output.channel(channel);
            if (frame >= out.size()) {
                continue;
            }
            float in = 0.0f;
            if (channel < context.input.channelCount()) {
                const std::span<float> from = context.input.channel(channel);
                in = frame < from.size() ? from[frame] : 0.0f;
            }
            out[frame] = m_trebleFilters[channel].process(m_bassFilters[channel].process(in, m_bass), m_treble);
        }
    }
    // Anything past a stereo pair is silence rather than whatever was there.
    for (std::uint32_t channel = kChannels; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        std::fill(out.begin(), out.begin() + std::min<std::size_t>(context.frames, out.size()), 0.0f);
    }
}

void BassTreble::reset() noexcept RB_NONBLOCKING
{
    settleSmoothers();
    resetFilters();
}

void BassTreble::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Both gains reach the audio thread this way; the smoothers read them
    // at the top of each cycle, so a change heard mid-drag is a glide.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> BassTreble::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue BassTreble::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap BassTreble::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> BassTreble::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        settleSmoothers();
    }
    return loaded;
}

} // namespace rb::blocks

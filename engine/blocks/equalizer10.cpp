// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/equalizer10.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>

namespace rb::blocks
{

using namespace rb::dsp;

namespace
{

[[nodiscard]] ParamDescriptor gainDescriptor(std::string_view id)
{
    ParamDescriptor descriptor;
    descriptor.id = std::string{id};
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(-Equalizer10::kRangeDb);
    descriptor.maximum = ParamValue::ofFloat(Equalizer10::kRangeDb);
    descriptor.defaultValue = ParamValue::ofFloat(0.0f);
    descriptor.unit = ParamUnit::Decibels;
    descriptor.taper = ParamTaper::Linear;
    descriptor.smoothingMs = Equalizer10::kSmoothingMs;
    return descriptor;
}

} // namespace

std::vector<ParamDescriptor> Equalizer10::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    for (const std::string_view id : kBandIds) {
        descriptors.push_back(gainDescriptor(id));
    }
    descriptors.push_back(gainDescriptor("preamp"));

    ParamDescriptor keepLevel;
    keepLevel.id = "keepLevel";
    keepLevel.type = ParamType::Bool;
    keepLevel.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(keepLevel));
    return descriptors;
}

Equalizer10::Equalizer10()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Bars;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Equalizer10::make()
{
    return std::make_unique<Equalizer10>();
}

FilterSpec Equalizer10::band(std::size_t index, double gainDb) noexcept
{
    return {FilterShape::Peaking, kBandFrequencies[index], kBandQ, gainDb};
}

double Equalizer10::curveDb(double frequency, const std::array<float, kBandCount> &gainsDb, double sampleRate) noexcept
{
    double sum = 0.0;
    for (std::size_t index = 0; index < kBandCount; ++index) {
        sum += filterMagnitudeDb(band(index, gainsDb[index]), frequency, sampleRate);
    }
    return sum;
}

float Equalizer10::keepLevelDb(const std::array<float, kBandCount> &gainsDb, double sampleRate) noexcept RB_NONBLOCKING
{
    // Nothing lifted, nothing to keep: the common case costs a comparison.
    if (std::ranges::none_of(gainsDb, [](float gain) { return gain > 0.0f; })) {
        return 0.0f;
    }
    double highest = 0.0;
    for (std::size_t index = 0; index < kBandCount; ++index) {
        highest = std::max(highest, curveDb(kBandFrequencies[index], gainsDb, sampleRate));
        if (index + 1 < kBandCount) {
            const double between = std::sqrt(kBandFrequencies[index] * kBandFrequencies[index + 1]);
            highest = std::max(highest, curveDb(between, gainsDb, sampleRate));
        }
    }
    return static_cast<float>(-highest);
}

std::expected<void, BlockError> Equalizer10::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    for (LinearSmoother &gain : m_gainsDb) {
        gain.prepare(spec.sampleRate, kSmoothingMs);
    }
    m_outputDb.prepare(spec.sampleRate, kSmoothingMs);
    // Worked out afresh for the new rate, whatever the gains were before.
    m_keptFor.fill(0.0f);
    m_keptDb = 0.0f;
    settleSmoothers();
    resetFilters();
    m_prepared = true;
    return {};
}

std::array<float, Equalizer10::kBandCount> Equalizer10::storedGains() const noexcept RB_NONBLOCKING
{
    std::array<float, kBandCount> gains{};
    for (std::size_t index = 0; index < kBandCount; ++index) {
        gains[index] = m_store.number(static_cast<std::uint32_t>(FirstBand + index));
    }
    return gains;
}

float Equalizer10::outputGainDb(const std::array<float, kBandCount> &gains) noexcept RB_NONBLOCKING
{
    float db = m_store.number(Preamp);
    if (m_store.number(KeepLevel) != 0.0f) {
        // **Worked out again only when a band has moved**: a drag is sixty
        // changes a second, a steady setting is none.
        if (gains != m_keptFor) {
            m_keptFor = gains;
            m_keptDb = keepLevelDb(gains, m_sampleRate);
        }
        db += m_keptDb;
    }
    return db;
}

void Equalizer10::aimSmoothers() noexcept RB_NONBLOCKING
{
    const std::array<float, kBandCount> gains = storedGains();
    for (std::size_t index = 0; index < kBandCount; ++index) {
        m_gainsDb[index].setTarget(gains[index]);
    }
    m_outputDb.setTarget(outputGainDb(gains));
}

void Equalizer10::settleSmoothers() noexcept RB_NONBLOCKING
{
    const std::array<float, kBandCount> gains = storedGains();
    for (std::size_t index = 0; index < kBandCount; ++index) {
        m_gainsDb[index].reset(gains[index]);
        m_bands[index] = designSvf(band(index, gains[index]), m_sampleRate);
    }
    m_outputDb.reset(outputGainDb(gains));
}

void Equalizer10::resetFilters() noexcept RB_NONBLOCKING
{
    for (std::array<Svf, kBandCount> &channel : m_filters) {
        for (Svf &filter : channel) {
            filter.reset();
        }
    }
}

void Equalizer10::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    aimSmoothers();
    const std::uint32_t channels = std::min(context.output.channelCount(), kChannels);
    float outputGain = dbToGain(m_outputDb.current());

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // **Worked out afresh only while a gain moves**: a band that has
        // arrived holds still and costs only its filtering.
        for (std::size_t index = 0; index < kBandCount; ++index) {
            if (m_gainsDb[index].isSmoothing()) {
                m_bands[index] = designSvf(band(index, m_gainsDb[index].next()), m_sampleRate);
            }
        }
        if (m_outputDb.isSmoothing()) {
            outputGain = dbToGain(m_outputDb.next());
        }
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> out = context.output.channel(channel);
            if (frame >= out.size()) {
                continue;
            }
            float sample = 0.0f;
            if (channel < context.input.channelCount()) {
                const std::span<float> from = context.input.channel(channel);
                sample = frame < from.size() ? from[frame] : 0.0f;
            }
            for (std::size_t index = 0; index < kBandCount; ++index) {
                sample = m_filters[channel][index].process(sample, m_bands[index]);
            }
            out[frame] = sample * outputGain;
        }
    }
    // Anything past a stereo pair is silence rather than whatever was there.
    for (std::uint32_t channel = kChannels; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        std::fill(out.begin(), out.begin() + std::min<std::size_t>(context.frames, out.size()), 0.0f);
    }
}

void Equalizer10::reset() noexcept RB_NONBLOCKING
{
    settleSmoothers();
    resetFilters();
}

void Equalizer10::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Every setting reaches the audio thread this way; the smoothers read
    // them at the top of each cycle, so a change heard mid-drag is a glide.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Equalizer10::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Equalizer10::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Equalizer10::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Equalizer10::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        settleSmoothers();
    }
    return loaded;
}

} // namespace rb::blocks

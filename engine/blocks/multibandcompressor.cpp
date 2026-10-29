// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/multibandcompressor.h"

#include "rt/meter.h"

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

[[nodiscard]] ParamDescriptor ranged(std::string id,
                                     float lowest,
                                     float highest,
                                     float byDefault,
                                     ParamUnit unit,
                                     ParamTaper taper = ParamTaper::Linear)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(lowest);
    descriptor.maximum = ParamValue::ofFloat(highest);
    descriptor.defaultValue = ParamValue::ofFloat(byDefault);
    descriptor.unit = unit;
    descriptor.taper = taper;
    return descriptor;
}

/// What a band starts at. **Gentle, and slower at the bottom**: a low band's
/// waves are long, and an attack or a release shorter than a few of them
/// shapes each wave rather than the level, which is distortion, not
/// compression. The top band can be quick, which is what takes an "s" down.
struct BandDefaults {
    float attackMs;
    float releaseMs;
};
constexpr std::array<BandDefaults, MultibandCompressor::kBands> kBandDefaults{{
    {20.0f, 250.0f},
    {10.0f, 150.0f},
    {3.0f, 80.0f},
}};

} // namespace

std::vector<ParamDescriptor> MultibandCompressor::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    // --- where the bands meet ---
    // The two ranges overlap, so a person can put the splits anywhere a
    // three-band compressor is used; the crossover holds them 1.5 times
    // apart, and the panel will not let one handle cross the other.
    descriptors.push_back(ranged("lowCrossover", 30.0f, 2000.0f, 200.0f, ParamUnit::Hertz, ParamTaper::Logarithmic));
    descriptors.push_back(
        ranged("highCrossover", 500.0f, 16000.0f, 3000.0f, ParamUnit::Hertz, ParamTaper::Logarithmic));
    // One knee for all three, as the Dynamics block's default: a band's
    // character is in its threshold and ratio, and three knees would be
    // three more controls for a difference hard to hear.
    descriptors.push_back(ranged("knee", 0.0f, 24.0f, 6.0f, ParamUnit::Decibels));

    // --- each band ---
    for (std::uint32_t band = 0; band < kBands; ++band) {
        const std::string name{kBandNames[band]};
        descriptors.push_back(ranged(name + "Threshold", -60.0f, 0.0f, -24.0f, ParamUnit::Decibels));
        descriptors.push_back(ranged(name + "Ratio", 1.0f, 20.0f, 3.0f, ParamUnit::Ratio, ParamTaper::Logarithmic));
        descriptors.push_back(ranged(name + "Attack",
                                     0.1f,
                                     200.0f,
                                     kBandDefaults[band].attackMs,
                                     ParamUnit::Milliseconds,
                                     ParamTaper::Logarithmic));
        descriptors.push_back(ranged(name + "Release",
                                     10.0f,
                                     3000.0f,
                                     kBandDefaults[band].releaseMs,
                                     ParamUnit::Milliseconds,
                                     ParamTaper::Logarithmic));
        descriptors.push_back(ranged(name + "Makeup", -20.0f, 20.0f, 0.0f, ParamUnit::Decibels));
    }
    return descriptors;
}

MultibandCompressor::MultibandCompressor()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Bars;
    // **Nothing.** The splits are filters, which delay nothing as a whole
    // (they turn the phase, which is what adds up to an all-pass), and the
    // compressors are feed-forward with no lookahead.
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> MultibandCompressor::make()
{
    return std::make_unique<MultibandCompressor>();
}

DynamicsSettings MultibandCompressor::settings(std::uint32_t band) const noexcept RB_NONBLOCKING
{
    DynamicsSettings settings;
    if (band >= kBands) {
        return settings;
    }
    settings.thresholdDb = m_store.number(bandParam(band, Threshold));
    settings.ratio = m_store.number(bandParam(band, Ratio));
    settings.kneeDb = m_store.number(Knee);
    settings.makeupDb = m_store.number(bandParam(band, Makeup));
    // No expander: a multiband compressor holds back, it does not gate.
    settings.expanderThresholdDb = -120.0f;
    settings.expanderRatio = 1.0f;
    settings.expanderRangeDb = 0.0f;
    return settings;
}

DynamicsTiming MultibandCompressor::timing(std::uint32_t band) const noexcept RB_NONBLOCKING
{
    DynamicsTiming timing;
    if (band >= kBands) {
        return timing;
    }
    timing.attackMs = m_store.number(bandParam(band, Attack));
    timing.releaseMs = m_store.number(bandParam(band, Release));
    timing.detector = DynamicsDetector::Peak;
    return timing;
}

float MultibandCompressor::gainReduction(std::uint32_t band) const noexcept RB_NONBLOCKING
{
    return band < kBands ? m_processors[band].gainReduction() : 0.0f;
}

std::expected<void, BlockError> MultibandCompressor::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    m_crossover.setSplits(m_store.number(LowCrossover), m_store.number(HighCrossover));
    m_crossover.prepare(spec.sampleRate, kChannels);
    for (std::uint32_t band = 0; band < kBands; ++band) {
        m_processors[band].setSettings(settings(band));
        m_processors[band].setTiming(timing(band));
        // After the settings, which the coefficients are worked out from.
        m_processors[band].prepare(spec.sampleRate, kChannels);
    }

    m_bandBuffer.resize(kBands * kChannels, spec.maxBlockFrames);
    for (std::uint32_t band = 0; band < kBands; ++band) {
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            m_bandChannels[band][channel] = m_bandBuffer.channel(band * kChannels + channel).data();
        }
    }
    m_meter = spec.meter;
    return {};
}

void MultibandCompressor::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t frames = std::min(context.frames, m_bandBuffer.frames());
    m_crossover.setSplits(m_store.number(LowCrossover), m_store.number(HighCrossover));

    // Every band starts empty, so a channel the input does not have is
    // silence rather than whatever the last cycle left.
    for (std::uint32_t channel = 0; channel < m_bandBuffer.channelCount(); ++channel) {
        std::ranges::fill(m_bandBuffer.channel(channel), 0.0f);
    }
    const AudioBlock low(m_bandChannels[0].data(), kChannels, frames);
    const AudioBlock mid(m_bandChannels[1].data(), kChannels, frames);
    const AudioBlock high(m_bandChannels[2].data(), kChannels, frames);
    m_crossover.process(context.input, low, mid, high, frames);

    const std::array<const AudioBlock *, kBands> bands{&low, &mid, &high};
    for (std::uint32_t band = 0; band < kBands; ++band) {
        m_processors[band].setSettings(settings(band));
        m_processors[band].setTiming(timing(band));
        m_processors[band].process(*bands[band], frames);
    }

    // The bands back together.
    const std::uint32_t channels = std::min(context.output.channelCount(), kChannels);
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const std::span<float> out = context.output.channel(channel);
        const std::span<float> fromLow = low.channel(channel);
        const std::span<float> fromMid = mid.channel(channel);
        const std::span<float> fromHigh = high.channel(channel);
        const std::uint32_t count = std::min<std::uint32_t>(frames, static_cast<std::uint32_t>(out.size()));
        for (std::uint32_t frame = 0; frame < count; ++frame) {
            out[frame] = fromLow[frame] + fromMid[frame] + fromHigh[frame];
        }
    }

    if (m_meter != nullptr) {
        for (std::uint32_t band = 0; band < kBands; ++band) {
            const DynamicsProcessor &processor = m_processors[band];
            m_meter->publish(kGainReductionValue + band, processor.gainReduction());
            m_published[band * kValuesPerBand + kBandInputValue] = processor.inputLevelDb();
            m_published[band * kValuesPerBand + kBandOutputValue] = processor.outputLevelDb();
            m_published[band * kValuesPerBand + kBandReductionValue] = processor.gainReduction();
        }
        // Dropped at once unless the panel is asking.
        m_meter->publishBands(m_published);
    }
}

void MultibandCompressor::reset() noexcept RB_NONBLOCKING
{
    m_crossover.reset();
    for (DynamicsProcessor &processor : m_processors) {
        processor.reset();
    }
}

void MultibandCompressor::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // The crossover and the compressors are handed everything at the top of
    // each cycle, so a change that arrives mid-drag is in the next one.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> MultibandCompressor::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue MultibandCompressor::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap MultibandCompressor::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> MultibandCompressor::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

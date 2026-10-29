// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/parametriceq.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace rb::blocks
{

using namespace rb::dsp;

namespace
{

/// Where each slot's band sits until somebody moves it. The first three are
/// in use in a new block; the rest are where a band added next is put when
/// the interface has no better idea.
constexpr std::array<float, ParametricEq::kBandCount>
    kDefaultFrequencies{100.0f, 1000.0f, 8000.0f, 250.0f, 2500.0f, 50.0f, 500.0f, 4000.0f, 12000.0f, 16000.0f};
constexpr std::size_t kBandsInUseAtFirst = 3;

constexpr std::array<std::string_view, ParametricEq::BandParamCount> kParamNames{"Used",
                                                                                 "On",
                                                                                 "Type",
                                                                                 "Frequency",
                                                                                 "Gain",
                                                                                 "Q"};

} // namespace

std::string ParametricEq::paramId(std::size_t band, BandParam which)
{
    return "band" + std::to_string(band + 1) + std::string{kParamNames[which]};
}

FilterShape ParametricEq::shapeOf(std::size_t typeIndex) noexcept
{
    switch (typeIndex) {
    case 1:
        return FilterShape::LowShelf;
    case 2:
        return FilterShape::HighShelf;
    case 3:
        return FilterShape::LowPass;
    case 4:
        return FilterShape::HighPass;
    case 5:
        return FilterShape::Notch;
    case 6:
        return FilterShape::BandPass;
    default:
        return FilterShape::Peaking;
    }
}

std::vector<ParamDescriptor> ParametricEq::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    for (std::size_t band = 0; band < kBandCount; ++band) {
        ParamDescriptor used;
        used.id = paramId(band, Used);
        used.type = ParamType::Bool;
        used.defaultValue = ParamValue::ofBool(band < kBandsInUseAtFirst);
        descriptors.push_back(std::move(used));

        ParamDescriptor on;
        on.id = paramId(band, On);
        on.type = ParamType::Bool;
        on.defaultValue = ParamValue::ofBool(true);
        descriptors.push_back(std::move(on));

        ParamDescriptor type;
        type.id = paramId(band, Type);
        type.type = ParamType::Enum;
        for (const std::string_view id : kTypeIds) {
            type.choices.emplace_back(id);
        }
        type.defaultValue = ParamValue::ofEnum(std::string{kTypeIds[0]});
        descriptors.push_back(std::move(type));

        ParamDescriptor frequency;
        frequency.id = paramId(band, Frequency);
        frequency.type = ParamType::Float;
        frequency.minimum = ParamValue::ofFloat(kMinimumFrequency);
        frequency.maximum = ParamValue::ofFloat(kMaximumFrequency);
        frequency.defaultValue = ParamValue::ofFloat(kDefaultFrequencies[band]);
        frequency.unit = ParamUnit::Hertz;
        frequency.taper = ParamTaper::Logarithmic;
        frequency.smoothingMs = kGlideMs;
        descriptors.push_back(std::move(frequency));

        ParamDescriptor gain;
        gain.id = paramId(band, Gain);
        gain.type = ParamType::Float;
        gain.minimum = ParamValue::ofFloat(-kRangeDb);
        gain.maximum = ParamValue::ofFloat(kRangeDb);
        gain.defaultValue = ParamValue::ofFloat(0.0f);
        gain.unit = ParamUnit::Decibels;
        gain.smoothingMs = kGainSmoothingMs;
        descriptors.push_back(std::move(gain));

        ParamDescriptor q;
        q.id = paramId(band, Q);
        q.type = ParamType::Float;
        q.minimum = ParamValue::ofFloat(kMinimumQ);
        q.maximum = ParamValue::ofFloat(kMaximumQ);
        q.defaultValue = ParamValue::ofFloat(kDefaultQ);
        q.unit = ParamUnit::Ratio;
        q.taper = ParamTaper::Logarithmic;
        q.smoothingMs = kGlideMs;
        descriptors.push_back(std::move(q));
    }
    return descriptors;
}

ParametricEq::ParametricEq()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Curve;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> ParametricEq::make()
{
    return std::make_unique<ParametricEq>();
}

double ParametricEq::curveDb(double frequency, std::span<const Band> bands, double sampleRate) noexcept
{
    double sum = 0.0;
    for (const Band &band : bands) {
        if (band.active) {
            sum += filterMagnitudeDb({band.shape, band.frequency, band.q, band.gainDb}, frequency, sampleRate);
        }
    }
    return sum;
}

std::array<ParametricEq::Band, ParametricEq::kBandCount> ParametricEq::bands() const
{
    std::array<Band, kBandCount> all{};
    for (std::size_t band = 0; band < kBandCount; ++band) {
        all[band].active = wantsRunning(band);
        all[band].shape = shapeOf(wantedType(band));
        all[band].frequency = m_store.number(index(band, Frequency));
        all[band].gainDb = m_store.number(index(band, Gain));
        all[band].q = m_store.number(index(band, Q));
    }
    return all;
}

bool ParametricEq::wantsRunning(std::size_t band) const noexcept RB_NONBLOCKING
{
    return m_store.number(index(band, Used)) != 0.0f && m_store.number(index(band, On)) != 0.0f;
}

std::size_t ParametricEq::wantedType(std::size_t band) const noexcept RB_NONBLOCKING
{
    const auto type = static_cast<std::size_t>(std::max(0.0f, m_store.number(index(band, Type))));
    return std::min(type, kTypeIds.size() - 1);
}

std::expected<void, BlockError> ParametricEq::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    for (Voice &voice : m_voices) {
        voice.octaves.prepare(spec.sampleRate, kGlideMs);
        voice.gainDb.prepare(spec.sampleRate, kGainSmoothingMs);
        voice.logQ.prepare(spec.sampleRate, kGlideMs);
        voice.mix.prepare(spec.sampleRate, kFadeMs);
    }
    settleVoices();
    m_prepared = true;
    return {};
}

void ParametricEq::design(Voice &voice) noexcept RB_NONBLOCKING
{
    voice.coefficients = designSvf({shapeOf(voice.typeIndex),
                                    std::exp2(static_cast<double>(voice.octaves.current())),
                                    std::exp(static_cast<double>(voice.logQ.current())),
                                    static_cast<double>(voice.gainDb.current())},
                                   m_sampleRate);
}

void ParametricEq::settleVoices() noexcept RB_NONBLOCKING
{
    for (std::size_t band = 0; band < kBandCount; ++band) {
        Voice &voice = m_voices[band];
        voice.typeIndex = wantedType(band);
        voice.running = wantsRunning(band);
        voice.octaves.reset(std::log2(m_store.number(index(band, Frequency))));
        voice.gainDb.reset(m_store.number(index(band, Gain)));
        voice.logQ.reset(std::log(m_store.number(index(band, Q))));
        voice.mix.reset(voice.running ? 1.0f : 0.0f);
        for (Svf &filter : voice.filters) {
            filter.reset();
        }
        design(voice);
    }
}

void ParametricEq::aimVoices() noexcept RB_NONBLOCKING
{
    for (std::size_t band = 0; band < kBandCount; ++band) {
        Voice &voice = m_voices[band];
        const float octaves = std::log2(m_store.number(index(band, Frequency)));
        const float gainDb = m_store.number(index(band, Gain));
        const float logQ = std::log(m_store.number(index(band, Q)));
        const bool wanted = wantsRunning(band);

        if (!voice.running) {
            if (!wanted) {
                continue;
            }
            // **A band starting fades in from where it is set**, rather
            // than gliding there from wherever it was last: the glide
            // would be a sweep heard as the fade opens.
            voice.typeIndex = wantedType(band);
            voice.running = true;
            voice.octaves.reset(octaves);
            voice.gainDb.reset(gainDb);
            voice.logQ.reset(logQ);
            voice.mix.reset(0.0f);
            for (Svf &filter : voice.filters) {
                filter.reset();
            }
            design(voice);
        }
        voice.octaves.setTarget(octaves);
        voice.gainDb.setTarget(gainDb);
        voice.logQ.setTarget(logQ);
        // Out, for a band that is stopping or becoming another type; in,
        // for one that is what it has been asked to be.
        voice.mix.setTarget(wanted && wantedType(band) == voice.typeIndex ? 1.0f : 0.0f);
    }
}

void ParametricEq::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    aimVoices();
    const std::uint32_t channels = std::min(context.output.channelCount(), kChannels);

    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        const std::span<float> out = context.output.channel(channel);
        const std::size_t frames = std::min<std::size_t>(context.frames, out.size());
        if (channel < context.input.channelCount()) {
            const std::span<float> in = context.input.channel(channel);
            for (std::size_t frame = 0; frame < frames; ++frame) {
                out[frame] = frame < in.size() ? in[frame] : 0.0f;
            }
        } else {
            std::fill(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(frames), 0.0f);
        }
    }

    // **One band at a time over the whole cycle**, each reading the one
    // before's output in place: the cascade is the same sample by sample,
    // and a band nobody is using costs nothing.
    for (std::size_t band = 0; band < kBandCount; ++band) {
        Voice &voice = m_voices[band];
        if (!voice.running) {
            continue;
        }
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            if (voice.octaves.isSmoothing() || voice.gainDb.isSmoothing() || voice.logQ.isSmoothing()) {
                (void)voice.octaves.next();
                (void)voice.gainDb.next();
                (void)voice.logQ.next();
                design(voice);
            }
            const float mix = voice.mix.next();
            for (std::uint32_t channel = 0; channel < channels; ++channel) {
                const std::span<float> out = context.output.channel(channel);
                if (frame >= out.size()) {
                    continue;
                }
                const float dry = out[frame];
                const float wet = voice.filters[channel].process(dry, voice.coefficients);
                out[frame] = dry + (wet - dry) * mix;
            }
            // **Faded out: stop, or come back as what was asked for.** Done
            // at the sample the fade ends, not at the next cycle, so that
            // it happens at the same point in the signal whatever the
            // quantum.
            if (mix == 0.0f && !voice.mix.isSmoothing() && voice.mix.target() == 0.0f) {
                const bool wanted = wantsRunning(band);
                for (Svf &filter : voice.filters) {
                    filter.reset();
                }
                if (!wanted) {
                    voice.running = false;
                    break;
                }
                voice.typeIndex = wantedType(band);
                voice.octaves.reset(voice.octaves.target());
                voice.gainDb.reset(voice.gainDb.target());
                voice.logQ.reset(voice.logQ.target());
                design(voice);
                voice.mix.setTarget(1.0f);
            }
        }
    }

    for (std::uint32_t channel = kChannels; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        std::fill(out.begin(), out.begin() + std::min<std::size_t>(context.frames, out.size()), 0.0f);
    }
}

void ParametricEq::reset() noexcept RB_NONBLOCKING
{
    settleVoices();
}

void ParametricEq::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> ParametricEq::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue ParametricEq::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap ParametricEq::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> ParametricEq::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        settleVoices();
    }
    return loaded;
}

} // namespace rb::blocks

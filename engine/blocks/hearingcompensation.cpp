// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/hearingcompensation.h"

#include "dsp/audiogram.h"
#include "dsp/decibel.h"
#include "dsp/erbscale.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

constexpr std::array<std::string_view, HearingCompensation::BandParamCount> kBandSuffixes{"SoftGainDb",
                                                                                          "AverageGainDb",
                                                                                          "LoudGainDb",
                                                                                          "CeilingDb"};
constexpr std::array<std::string_view, HearingCompensation::PointParamCount> kPointSuffixes{"On",
                                                                                            "Hz",
                                                                                            "SoftGainDb",
                                                                                            "AverageGainDb",
                                                                                            "LoudGainDb",
                                                                                            "CeilingDb"};

ParamDescriptor decibels(std::string id, float minimum, float maximum, float defaultValue)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(minimum);
    descriptor.maximum = ParamValue::ofFloat(maximum);
    descriptor.defaultValue = ParamValue::ofFloat(defaultValue);
    descriptor.unit = ParamUnit::Decibels;
    return descriptor;
}

} // namespace

HearingCompensation::HearingCompensation(std::size_t bands, std::string_view typeId, Editing editing)
    : m_bandCount(bands)
    , m_editing(editing)
    , m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
    , m_bands(bands)
    , m_points(editing == Editing::Points ? kMaxPoints : 0)
    , m_gainDb(bands, 0.0f)
    , m_levelDbfs(bands, -120.0f)
    , m_published(2 * bands, 0.0f)
{
    m_info.typeId = std::string{typeId};
    m_info.category = BlockCategory::Hearing;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Curve;
    // Worked out for the rate in prepare(); this is 48 kHz's, so that the
    // block says something sensible before it has been prepared.
    m_info.latencySamples = 192 + 72 + TruePeakEstimator::kDelay;
}

std::unique_ptr<Block> HearingCompensation::make16()
{
    return std::make_unique<HearingCompensation>(kBands16, kTypeId16);
}

std::unique_ptr<Block> HearingCompensation::make64()
{
    return std::make_unique<HearingCompensation>(kBands64, kTypeId64, Editing::Points);
}

std::string HearingCompensation::bandParamId(std::size_t band, BandParam which)
{
    return "band" + std::to_string(band) + std::string{kBandSuffixes[which]};
}

std::string HearingCompensation::pointParamId(std::size_t point, PointParam which)
{
    return "point" + std::to_string(point) + std::string{kPointSuffixes[which]};
}

std::vector<double> HearingCompensation::bandCentresHz(std::size_t bands)
{
    return erbCentres(bands);
}

std::vector<ParamDescriptor> HearingCompensation::makeDescriptors() const
{
    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(earParam());

    ParamDescriptor speed;
    speed.id = "speed";
    speed.type = ParamType::Enum;
    for (const std::string_view id : kHearingSpeedIds) {
        speed.choices.emplace_back(id);
    }
    speed.defaultValue =
        ParamValue::ofEnum(std::string{kHearingSpeedIds[static_cast<std::size_t>(HearingSpeed::Adaptive)]});
    descriptors.push_back(std::move(speed));

    descriptors.push_back(decibels("loudnessDb", -30.0f, 30.0f, 0.0f));
    descriptors.push_back(decibels("noiseFloorDb", -100.0f, -40.0f, -75.0f));
    descriptors.push_back(decibels("maximumGainDb", 0.0f, kMaximumGainDb, 50.0f));
    descriptors.push_back(decibels("limiterCeilingDb", kLimiterLowestDb, kLimiterHighestDb, kLimiterHighestDb));

    // **The bands are the panel's to draw**, a curve with handles, not
    // sixty-four sliders; the generic settings panel leaves them out.
    if (m_editing == Editing::Bands) {
        for (std::size_t band = 0; band < m_bandCount; ++band) {
            for (std::uint32_t which = 0; which < BandParamCount; ++which) {
                ParamDescriptor descriptor = which == Ceiling
                    ? decibels(bandParamId(band, Ceiling), kMinimumCeilingDb, kMaximumCeilingDb, kDefaultCeilingDb)
                    : decibels(bandParamId(band, static_cast<BandParam>(which)), kMinimumGainDb, kMaximumGainDb, 0.0f);
                descriptor.flags.hidden = true;
                descriptors.push_back(std::move(descriptor));
            }
        }
    } else {
        for (std::size_t point = 0; point < kMaxPoints; ++point) {
            const bool placed = point < kDefaultPointsHz.size();
            ParamDescriptor on;
            on.id = pointParamId(point, PointOn);
            on.type = ParamType::Bool;
            on.defaultValue = ParamValue::ofBool(placed);
            on.flags.hidden = true;
            descriptors.push_back(std::move(on));

            ParamDescriptor hz;
            hz.id = pointParamId(point, PointHz);
            hz.type = ParamType::Float;
            hz.minimum = ParamValue::ofFloat(static_cast<float>(kErbLowestHz));
            hz.maximum = ParamValue::ofFloat(static_cast<float>(kErbHighestHz));
            hz.defaultValue =
                ParamValue::ofFloat(static_cast<float>(placed ? kDefaultPointsHz[point] : kUnplacedPointHz));
            hz.unit = ParamUnit::Hertz;
            hz.taper = ParamTaper::Logarithmic;
            hz.flags.hidden = true;
            descriptors.push_back(std::move(hz));

            for (const PointParam which : {PointSoftGain, PointAverageGain, PointLoudGain}) {
                ParamDescriptor gain = decibels(pointParamId(point, which), kMinimumGainDb, kMaximumGainDb, 0.0f);
                gain.flags.hidden = true;
                descriptors.push_back(std::move(gain));
            }
            ParamDescriptor ceiling =
                decibels(pointParamId(point, PointCeiling), kMinimumCeilingDb, kMaximumCeilingDb, kDefaultCeilingDb);
            ceiling.flags.hidden = true;
            descriptors.push_back(std::move(ceiling));
        }
    }

    // One ear's audiogram, which the panel edits and fits from.
    for (ParamDescriptor &descriptor : audiogramParams()) {
        descriptors.push_back(std::move(descriptor));
    }
    return descriptors;
}

std::expected<void, BlockError> HearingCompensation::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_bank.prepare(spec.sampleRate);
    // A level taken over about a band either side: the 16-band block's band
    // spacing, ADR-0019's ±2 ERB where every channel is a band.
    const std::size_t channels = m_bank.channelCount();
    const double coupling = m_bandCount >= channels ? 2.0 : erbSpacing(m_bandCount);
    m_layout.prepare(m_bank.centresHz(), m_bandCount, coupling, [this](std::size_t channel, double hz) {
        return m_bank.sinePower(channel, hz);
    });
    m_channelBands.assign(channels, HearingBand{});
    m_levelPower.assign(channels, 0.0f);
    m_channelGainDb.assign(channels, 0.0f);
    m_control.prepare(channels, static_cast<double>(GammatoneBank::kControlFrames) / spec.sampleRate);
    // **Always ready for true peaks**, as the Limiter block is, so the
    // latency is one number.
    m_limiter.prepare(spec.sampleRate,
                      1,
                      m_store.number(LimiterCeiling),
                      LookaheadLimiter::kDefaultLookaheadMs,
                      LookaheadLimiter::kDefaultReleaseMs,
                      true);
    m_limiter.setTruePeak(true);
    m_info.latencySamples = m_bank.latencySamples() + m_limiter.latencySamples();
    m_router.prepare(spec.sampleRate, spec.maxBlockFrames, m_info.latencySamples, earFromNumber(m_store.number(Ear)));
    m_meter = spec.meter;
    follow();
    resetEar();
    return {};
}

void HearingCompensation::follow() noexcept RB_NONBLOCKING
{
    const auto speed = static_cast<std::size_t>(std::lround(m_store.number(Speed)));
    m_settings.speed = speed < kHearingSpeedIds.size() ? static_cast<HearingSpeed>(speed) : HearingSpeed::Adaptive;
    m_settings.loudnessDb = m_store.number(Loudness);
    m_settings.noiseFloorDbfs = m_store.number(NoiseFloor);
    m_settings.maximumGainDb = m_store.number(MaximumGain);
    if (m_editing == Editing::Bands) {
        for (std::size_t band = 0; band < m_bandCount; ++band) {
            const auto first = static_cast<std::uint32_t>(FirstBandParam + band * BandParamCount);
            HearingBand &settings = m_bands[band];
            settings.softGainDb = m_store.number(first + SoftGain);
            settings.averageGainDb = m_store.number(first + AverageGain);
            settings.loudGainDb = m_store.number(first + LoudGain);
            settings.ceilingDbfs = listeningToDbfs(m_store.number(first + Ceiling));
        }
    } else {
        // **The bands follow the curves through the points**, worked out
        // again every cycle: two dozen points and 64 bands cost less than
        // keeping track of which point changed.
        for (std::size_t point = 0; point < kMaxPoints; ++point) {
            const auto first = static_cast<std::uint32_t>(FirstBandParam + point * PointParamCount);
            HearingPoint &at = m_points[point];
            at.on = m_store.number(first + PointOn) >= 0.5f;
            at.hz = static_cast<double>(m_store.number(first + PointHz));
            at.settings.softGainDb = m_store.number(first + PointSoftGain);
            at.settings.averageGainDb = m_store.number(first + PointAverageGain);
            at.settings.loudGainDb = m_store.number(first + PointLoudGain);
            at.settings.ceilingDbfs = listeningToDbfs(m_store.number(first + PointCeiling));
        }
        const HearingBand flat{.ceilingDbfs = listeningToDbfs(kDefaultCeilingDb)};
        bandsFromPoints(m_points, m_layout.centresHz(), flat, m_bands);
    }
    m_layout.channelSettings(m_bands, m_channelBands);
    m_limiter.setCeilingGain(dbToGain(m_store.number(LimiterCeiling)));
}

void HearingCompensation::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    follow();
    m_router.setEar(earFromNumber(m_store.number(Ear)));
    m_router.process(
        context,
        [this](std::span<float> samples) noexcept RB_NONBLOCKING { processEar(samples); },
        [this]() noexcept RB_NONBLOCKING { resetEar(); });

    if (m_meter != nullptr) {
        m_meter->publish(kLimiterReductionValue, limiterReductionDb());
        float sum = 0.0f;
        for (const float gain : m_gainDb) {
            sum += gain;
        }
        m_meter->publish(kMeanGainValue, m_bandCount > 0 ? sum / static_cast<float>(m_bandCount) : 0.0f);
        if (m_meter->isAnalysing()) {
            for (std::size_t band = 0; band < m_bandCount; ++band) {
                m_published[band] = m_gainDb[band];
                m_published[m_bandCount + band] = m_levelDbfs[band];
            }
            m_meter->publishBands(m_published);
        }
    }
}

void HearingCompensation::processEar(std::span<float> samples) noexcept RB_NONBLOCKING
{
    m_bank.process(samples, [this](std::span<const float> power, std::span<float> gains) noexcept RB_NONBLOCKING {
        m_layout.channelLevels(power, m_levelPower);
        m_control.update(m_levelPower, m_channelBands, m_settings, m_channelGainDb);
        for (std::size_t channel = 0; channel < gains.size() && channel < m_channelGainDb.size(); ++channel) {
            gains[channel] = dbToGain(m_channelGainDb[channel]);
        }
        const std::span<const float> levels = m_control.levelsDbfs();
        for (std::size_t band = 0; band < m_bandCount; ++band) {
            const std::size_t channel = m_layout.channelOfBand(band);
            m_gainDb[band] = m_channelGainDb[channel];
            m_levelDbfs[band] = levels[channel];
        }
    });
    float *channel = samples.data();
    m_limiter.process(AudioBlock{&channel, 1, static_cast<std::uint32_t>(samples.size())},
                      static_cast<std::uint32_t>(samples.size()));
}

void HearingCompensation::resetEar() noexcept RB_NONBLOCKING
{
    m_bank.reset();
    m_control.reset(m_settings);
    m_limiter.reset();
    std::fill(m_gainDb.begin(), m_gainDb.end(), 0.0f);
    std::fill(m_channelGainDb.begin(), m_channelGainDb.end(), 0.0f);
}

void HearingCompensation::reset() noexcept RB_NONBLOCKING
{
    follow();
    resetEar();
    m_router.setEar(earFromNumber(m_store.number(Ear)));
    m_router.reset();
}

float HearingCompensation::limiterReductionDb() const noexcept RB_NONBLOCKING
{
    return std::max(0.0f, -gainToDb(m_limiter.lowestGain()));
}

void HearingCompensation::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Taken up at the top of the next cycle (follow()).
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> HearingCompensation::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue HearingCompensation::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap HearingCompensation::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> HearingCompensation::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/nightlevel.h"

#include "blocks/speechnow.h"
#include "blocks/voicemodel.h"
#include "dsp/decibel.h"

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

/// A gain this near where it is going is there: a glide that closes a share of
/// the gap a sample would otherwise never arrive, and a block would stay a
/// hair off unity for ever.
constexpr float kArrivedDb = 1.0e-4f;

constexpr float kPerCent = 0.01f;

} // namespace

std::vector<ParamDescriptor> NightLevel::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    const auto amount = [](const char *id) {
        ParamDescriptor descriptor;
        descriptor.id = id;
        descriptor.type = ParamType::Float;
        descriptor.minimum = ParamValue::ofFloat(0.0f);
        descriptor.maximum = ParamValue::ofFloat(100.0f);
        descriptor.defaultValue = ParamValue::ofFloat(0.0f);
        descriptor.unit = ParamUnit::Percent;
        return descriptor;
    };
    descriptors.push_back(amount("cut"));
    descriptors.push_back(amount("boost"));

    ParamDescriptor from;
    from.id = "pivotFrom";
    from.type = ParamType::Enum;
    from.choices = {"found", "typed"};
    from.defaultValue = ParamValue::ofEnum("found");
    descriptors.push_back(std::move(from));

    ParamDescriptor typed;
    typed.id = "typedPivot";
    typed.type = ParamType::Float;
    typed.minimum = ParamValue::ofFloat(kLowestPivotLufs);
    typed.maximum = ParamValue::ofFloat(kHighestPivotLufs);
    typed.defaultValue = ParamValue::ofFloat(kDefaultTypedLufs);
    // As Auto Level's target: the catalogue has no unit for loudness.
    typed.unit = ParamUnit::Decibels;
    descriptors.push_back(std::move(typed));
    return descriptors;
}

NightLevel::NightLevel()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    // GainReach::Adds, the default: it lifts quiet sound, and no more than
    // the speech level, but a limit after it still has to know.
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> NightLevel::make()
{
    return std::make_unique<NightLevel>();
}

std::expected<void, BlockError> NightLevel::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_loudness.prepare(spec.sampleRate);
    m_voice.prepare(spec.sampleRate, makeVoiceModel());
    m_limiter.prepare(spec.sampleRate,
                      kChannels,
                      kPlainCeilingDb,
                      LookaheadLimiter::kDefaultLookaheadMs,
                      LookaheadLimiter::kDefaultReleaseMs,
                      true);
    m_limiter.setTruePeak(true);
    m_limiter.setRiseDbPerSecond(kMostRiseDbPerSecond);
    m_limiter.setMostReleaseDbPerSecond(kMostRiseDbPerSecond);
    // **A constant, whichever way it is detecting**, so the graph can line
    // other paths up around it.
    m_info.latencySamples = m_limiter.latencySamples();
    m_secondsPerSample = static_cast<float>(1.0 / spec.sampleRate);
    m_fallCoefficient = static_cast<float>(std::exp(-1.0 / (kFallMs * 1.0e-3 * spec.sampleRate)));
    m_actingHoldSamples = static_cast<std::uint32_t>(std::lround(kActingHoldMs * 1.0e-3 * spec.sampleRate));
    m_meter = spec.meter;
    reset();
    return {};
}

float NightLevel::lawDb(float readingLufs, float pivotLufs, float cut, float boost) noexcept RB_NONBLOCKING
{
    const PivotLaw law{.cutSlope = kCutSlope * cut,
                       .liftSlope = kLiftSlope * boost,
                       .bandAboveLu = kBandAboveLu,
                       .bandBelowLu = kBandBelowLu,
                       .floorLu = kFloorLu,
                       .mostCutDb = kMostChangeDb,
                       .mostLiftDb = kMostChangeDb};
    return pivotGainDb(readingLufs, pivotLufs, law);
}

float NightLevel::ceilingDb(float pivotLufs, float cut, float boost) noexcept RB_NONBLOCKING
{
    if (cut <= 0.0f && boost <= 0.0f) {
        return kOutOfReachDb;
    }
    if (pivotLufs <= kNoLevel || cut <= 0.0f) {
        return kPlainCeilingDb;
    }
    return std::min(kPlainCeilingDb, pivotLufs + kCeilingOverSpeechLu + kCeilingEaseLu * (1.0f - cut));
}

float NightLevel::pivotLufs() const noexcept RB_NONBLOCKING
{
    if (m_store.number(PivotFrom) != 0.0f) {
        return std::clamp(m_store.number(TypedPivot), kLowestPivotLufs, kHighestPivotLufs);
    }
    return foundLufs();
}

float NightLevel::ceilingNowDb() const noexcept RB_NONBLOCKING
{
    return ceilingDb(pivotLufs(),
                     std::clamp(m_store.number(Cut) * kPerCent, 0.0f, 1.0f),
                     std::clamp(m_store.number(Boost) * kPerCent, 0.0f, 1.0f));
}

void NightLevel::hearBlock(float momentaryLufs, float voicedShare) noexcept RB_NONBLOCKING
{
    constexpr float kBlockSeconds = 0.1f;
    if (momentaryLufs < kGateLufs) {
        return;
    }
    if (voicedShare < kLeastVoicedShare) {
        m_sinceVoiceSeconds = std::min(m_sinceVoiceSeconds + kBlockSeconds, kLapseSeconds);
        if (m_found && m_sinceVoiceSeconds >= kLapseSeconds) {
            m_found = false;
            m_heard = 0;
            m_energy = 0.0;
        }
        return;
    }
    m_sinceVoiceSeconds = 0.0f;
    if (!m_found) {
        ++m_heard;
        m_energy += std::pow(10.0, static_cast<double>(momentaryLufs) / 10.0);
        if (m_heard >= kFirstReadingBlocks) {
            const double mean = 10.0 * std::log10(m_energy / static_cast<double>(m_heard));
            m_foundLufs = std::clamp(static_cast<float>(mean), kLowestPivotLufs, kHighestPivotLufs);
            m_found = true;
        }
        return;
    }
    const float step = std::clamp(momentaryLufs - m_foundLufs, -kMostStepLu, kMostStepLu);
    m_foundLufs = std::clamp(m_foundLufs + step * kBlockSeconds / kFollowSeconds, kLowestPivotLufs, kHighestPivotLufs);
}

float NightLevel::wantedGainDb() const noexcept RB_NONBLOCKING
{
    const float pivot = pivotLufs();
    if (pivot <= kNoLevel) {
        return 0.0f;
    }
    const float cut = std::clamp(m_store.number(Cut) * kPerCent, 0.0f, 1.0f);
    const float boost = std::clamp(m_store.number(Boost) * kPerCent, 0.0f, 1.0f);
    const float loud = std::max(m_loudness.momentary(), m_loudness.fast() - kEarlyEdgeLu);
    if (loud > pivot + kBandAboveLu) {
        return lawDb(loud, pivot, cut, boost);
    }
    if (loud >= pivot - kBandBelowLu) {
        return 0.0f;
    }
    // Only the early readings may turn sound down: the gated one lags, and a
    // film that has just gone quiet would otherwise be cut for 3 s more.
    return std::max(0.0f, lawDb(m_loudness.loudness(), pivot, cut, boost));
}

void NightLevel::moveCeiling() noexcept RB_NONBLOCKING
{
    // The limiter walks a rising ceiling sample by sample at the rate it was
    // given, and glides a falling one.
    m_limiter.setCeilingGain(dbToGain(ceilingNowDb()));
}

void NightLevel::limit(const ProcessContext &context, std::uint32_t start, std::uint32_t count) noexcept RB_NONBLOCKING
{
    if (count == 0) {
        return;
    }
    const std::span<float> left = context.output.channel(kLeft);
    const std::span<float> right = context.output.channel(kRight);
    std::array<float *, kChannels> channels{left.empty() ? nullptr : left.data() + start,
                                            right.empty() ? nullptr : right.data() + start};
    m_limiter.process(AudioBlock{channels.data(), kChannels, count}, count);
    m_limitedDb = std::max(m_limitedDb, -gainToDb(m_limiter.lowestGain()));
}

void NightLevel::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::span<float> leftIn = context.input.channel(kLeft);
    const std::span<float> rightIn = context.input.channel(kRight);
    const std::span<float> leftOut = context.output.channel(kLeft);
    const std::span<float> rightOut = context.output.channel(kRight);
    const float mostRise = kMostRiseDbPerSecond * m_secondsPerSample;
    const float riseShare = m_secondsPerSample / (kRiseMs * 1.0e-3f);

    m_limitedDb = 0.0f;
    std::uint32_t limitedTo = 0;
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // Both inputs read before either output is written: the graph may
        // hand a block one buffer to read and to write.
        const float left = frame < leftIn.size() ? leftIn[frame] : 0.0f;
        const float right = frame < rightIn.size() ? rightIn[frame] : 0.0f;

        // The voice first: its frames end on the same samples as the slices,
        // so its answer belongs to the slice that ends here.
        (void)m_voice.push(0.5f * (left + right));
        const bool blockEnded = m_loudness.push(left, right);
        const bool slice = m_loudness.sliceEnded();
        if (slice) {
            m_voicedFrames[m_nextFrame] = m_voice.voiced();
            m_nextFrame = (m_nextFrame + 1) % m_voicedFrames.size();
        }
        if (blockEnded) {
            const auto voiced = static_cast<float>(std::ranges::count(m_voicedFrames, true));
            hearBlock(m_loudness.momentary(), voiced / static_cast<float>(m_voicedFrames.size()));
        }
        if (slice) {
            m_wantedDb = wantedGainDb();
        }

        if (m_wantedDb < m_gainDb) {
            m_gainDb = m_wantedDb + (m_gainDb - m_wantedDb) * m_fallCoefficient;
        } else {
            m_gainDb += m_limiterRising ? 0.0f : std::min(mostRise, (m_wantedDb - m_gainDb) * riseShare);
        }
        if (std::fabs(m_gainDb - m_wantedDb) < kArrivedDb) {
            m_gainDb = m_wantedDb;
        }
        const float gain = std::exp2(m_gainDb * (0.05f * 3.321928f));
        if (frame < leftOut.size()) {
            leftOut[frame] = left * gain;
        }
        if (frame < rightOut.size()) {
            rightOut[frame] = right * gain;
        }

        // **The limiter runs up to each slice's end, and its ceiling moves
        // there.** Set once a cycle it would move at the cycle, and the sound
        // would depend on the quantum.
        if (slice) {
            limit(context, limitedTo, frame + 1 - limitedTo);
            limitedTo = frame + 1;
            moveCeiling();
            m_limiterRising = m_limiter.gain() > m_limiterGain;
            m_limiterGain = m_limiter.gain();
        }
    }
    limit(context, limitedTo, context.frames - limitedTo);

    m_actingSamples = m_limitedDb >= kActingFromDb ? m_actingHoldSamples
                                                   : m_actingSamples - std::min(m_actingSamples, context.frames);

    if (m_meter != nullptr) {
        m_meter->publish(kGainValue, m_gainDb - m_limitedDb);
        m_meter->publish(kFoundValue, foundLufs());
        m_meter->publish(kReadingValue, m_loudness.momentary());
        m_meter->publish(kStateValue, state());
    }
}

float NightLevel::state() const noexcept RB_NONBLOCKING
{
    float tracking = NoSpeechYet;
    if (m_store.number(PivotFrom) != 0.0f) {
        tracking = Typed;
    } else if (m_found) {
        tracking = Found;
    }
    const float detector = SpeechNow::detectorReadingOf(m_voice.modelState(), m_voice.detector());
    return tracking + (m_actingSamples > 0 ? kPerActing : 0.0f) + detector * kPerDetector
        + std::floor(m_sinceVoiceSeconds) * kPerSecond;
}

void NightLevel::preferDetector(VoiceDetector detector) noexcept RB_NONBLOCKING
{
    m_voice.setPreferred(detector);
    m_voice.reset();
}

void NightLevel::reset() noexcept RB_NONBLOCKING
{
    m_loudness.reset();
    m_voice.reset();
    m_voicedFrames.fill(false);
    m_nextFrame = 0;
    m_found = false;
    m_foundLufs = kNoLevel;
    m_heard = 0;
    m_energy = 0.0;
    m_sinceVoiceSeconds = 0.0f;
    m_wantedDb = 0.0f;
    m_gainDb = 0.0f;
    m_limitedDb = 0.0f;
    m_limiterGain = 1.0f;
    m_limiterRising = false;
    m_actingSamples = 0;
    // A start takes the ceiling at once: there is no sound yet to be careful of.
    moveCeiling();
    m_limiter.reset();
}

void NightLevel::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at the next slice, at most 10 ms on, and glided to from there.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> NightLevel::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue NightLevel::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap NightLevel::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> NightLevel::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

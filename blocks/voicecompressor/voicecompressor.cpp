// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/voicecompressor/voicecompressor.h"

#include "dsp/dynamics.h"
#include "dsp/envelope.h"

#include <algorithm>
#include <array>
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
constexpr float kSilentSquare = 1.0e-12f;

/// The largest sample the block takes in, 60 dB over full scale. **A large
/// sample is as bad as a broken one:** its square could pass what a float
/// holds, which would break the follower for good, and short of that one
/// sample would hold the sound down for seconds.
constexpr float kLargestSample = 1.0e3f;

/// A reduction this near nothing is nothing: an exponential that closes a
/// share of the gap a sample would go on for ever, and into the denormals.
constexpr float kNoReductionDb = 1.0e-5f;

/// Moves \a value towards \a target with the factor for its direction.
[[nodiscard]] float chase(float value, float target, float falling, float rising) noexcept RB_NONBLOCKING
{
    const float kept = target < value ? falling : rising;
    const float next = target + (value - target) * kept;
    return std::fabs(next) < kNoReductionDb ? 0.0f : next;
}

} // namespace

float VoiceCompressor::ratioAt(float compressionDb) noexcept RB_NONBLOCKING
{
    return kGentlestRatio + (kFirmestRatio - kGentlestRatio) * (compressionDb / kLowestCompressionDb);
}

float VoiceCompressor::compressorDb(float levelDb, float compressionDb) noexcept RB_NONBLOCKING
{
    return gainReductionDb(levelDb,
                           DynamicsSettings{.thresholdDb = compressionDb,
                                            .ratio = ratioAt(compressionDb),
                                            .kneeDb = kKneeDb,
                                            .expanderThresholdDb = kSilenceDb,
                                            .expanderRatio = 1.0f,
                                            .expanderRangeDb = 0.0f,
                                            .makeupDb = 0.0f});
}

float VoiceCompressor::gateDb(float levelDb, float gateAtDb) noexcept RB_NONBLOCKING
{
    if (gateAtDb <= kGateOffDb) {
        return 0.0f;
    }
    const float under = gateAtDb - levelDb;
    const float half = kGateKneeDb * 0.5f;
    if (under <= -half) {
        return 0.0f;
    }
    // The same bend as the compressor's, so the slope joins without a corner.
    const float taken = under < half ? (under + half) * (under + half) / (2.0f * kGateKneeDb) : under;
    return -std::min(taken, kGateRangeDb);
}

float VoiceCompressor::makeupDb(float compressionDb, float ceilingDb) noexcept RB_NONBLOCKING
{
    // **What full scale loses, given back**, so a sound at full scale comes out
    // at the Ceiling and everything under it comes up.
    const float push = std::clamp(1.0f + ceilingDb / kPushRangeDb, 0.0f, 1.0f);
    return -compressorDb(0.0f, compressionDb) * (1.0f + kPushShare * push);
}

float VoiceCompressor::outputDb(float levelDb,
                                float compressionDb,
                                float ceilingDb,
                                float gateAtDb) noexcept RB_NONBLOCKING
{
    const float gated = levelDb + gateDb(levelDb, gateAtDb);
    const float out = gated + compressorDb(gated, compressionDb) + makeupDb(compressionDb, ceilingDb) + ceilingDb;
    return std::min(out, ceilingDb);
}

std::vector<ParamDescriptor> VoiceCompressor::makeDescriptors()
{
    const auto decibels = [](const char *id, float lowest, float standard) {
        ParamDescriptor descriptor;
        descriptor.id = id;
        descriptor.type = ParamType::Float;
        descriptor.minimum = ParamValue::ofFloat(lowest);
        descriptor.maximum = ParamValue::ofFloat(0.0f);
        descriptor.defaultValue = ParamValue::ofFloat(standard);
        descriptor.unit = ParamUnit::Decibels;
        return descriptor;
    };

    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(decibels("compression", kLowestCompressionDb, kDefaultCompressionDb));
    descriptors.push_back(decibels("gate", kGateOffDb, kGateOffDb));
    descriptors.push_back(decibels("ceiling", kLowestCeilingDb, kDefaultCeilingDb));
    return descriptors;
}

VoiceCompressor::VoiceCompressor()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // Its ceiling moves, so the reach stays the default: see the class comment.
    m_info.gainReach = GainReach::Adds;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> VoiceCompressor::make()
{
    return std::make_unique<VoiceCompressor>();
}

float VoiceCompressor::ceilingDb() const noexcept RB_NONBLOCKING
{
    return m_store.number(Ceiling);
}

std::expected<void, BlockError> VoiceCompressor::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_meter = spec.meter;
    const double rate = spec.sampleRate;
    m_limiter.prepare(rate,
                      kChannels,
                      m_store.number(Ceiling),
                      LookaheadLimiter::kDefaultLookaheadMs,
                      LookaheadLimiter::kDefaultReleaseMs,
                      true);
    m_limiter.setTruePeak(true);
    m_info.latencySamples = m_limiter.latencySamples();

    m_compression.prepare(rate, kCompressionGlideMs);
    m_ceiling.prepare(rate, LookaheadLimiter::kCeilingGlideMs);
    m_levelCoefficient = exponentialDecayCoefficient(kLevelMs, rate);
    m_attackCoefficient = exponentialDecayCoefficient(kAttackMs, rate);
    m_quickReleaseCoefficient = exponentialDecayCoefficient(kQuickReleaseMs, rate);
    m_slowAttackCoefficient = exponentialDecayCoefficient(kSlowAttackMs, rate);
    m_slowReleaseCoefficient = exponentialDecayCoefficient(kSlowReleaseMs, rate);
    m_gateOpensCoefficient = exponentialDecayCoefficient(kGateOpensMs, rate);
    m_gateClosesCoefficient = exponentialDecayCoefficient(kGateClosesMs, rate);
    m_gateHoldSamples = static_cast<std::uint32_t>(std::lround(static_cast<double>(kGateHoldsMs) * 1.0e-3 * rate));
    m_holdSamples = static_cast<std::uint32_t>(std::lround(static_cast<double>(kReadingHoldMs) * 1.0e-3 * rate));
    reset();
    return {};
}

float VoiceCompressor::gainFor(std::span<const float, 2> samples) noexcept RB_NONBLOCKING
{
    // **A mean for each side, and the louder mean decides.** The larger sample
    // of each moment would read two unrelated sides about 2 dB above either.
    for (std::size_t side = 0; side < m_meanSquares.size(); ++side) {
        const float square = samples[side] * samples[side];
        m_meanSquares[side] = square + (m_meanSquares[side] - square) * m_levelCoefficient;
    }
    const float meanSquare = std::max(m_meanSquares[0], m_meanSquares[1]);
    // Twice the mean square, so a sine reads as its peak and the graph's level
    // in is the level a person sees on a meter.
    m_levelDb = 10.0f * std::log10(std::max(2.0f * meanSquare, kSilentSquare));

    const bool moved = m_compression.isSmoothing() || m_ceiling.isSmoothing();
    const float compression = m_compression.next();
    const float ceiling = m_ceiling.next();
    if (moved) {
        m_makeupDb = makeupDb(compression, ceiling);
    }

    const float gate = gateDb(m_levelDb, m_gateAtDb);
    if (gate >= m_gateDb) {
        m_gateDb = chase(m_gateDb, gate, m_gateClosesCoefficient, m_gateOpensCoefficient);
        m_gateHold = m_gateHoldSamples;
    } else if (m_gateHold > 0) {
        --m_gateHold;
    } else {
        m_gateDb = chase(m_gateDb, gate, m_gateClosesCoefficient, m_gateOpensCoefficient);
    }

    // After the gate: what it took off the compressor does not see.
    const float target = compressorDb(m_levelDb + m_gateDb, compression);
    m_quickDb = chase(m_quickDb, target, m_attackCoefficient, m_quickReleaseCoefficient);
    m_slowDb = chase(m_slowDb, target, m_slowAttackCoefficient, m_slowReleaseCoefficient);
    const float compressed = std::min(m_quickDb, m_slowDb);

    // The two together, from one sample, so that their sum is a real total.
    if (compressed + m_gateDb <= m_heldCompressedDb + m_heldGatedDb || m_heldFor == 0) {
        m_heldCompressedDb = compressed;
        m_heldGatedDb = m_gateDb;
        m_heldFor = m_holdSamples;
    } else {
        --m_heldFor;
    }
    return dbToGain(m_gateDb + compressed + m_makeupDb + ceiling);
}

void VoiceCompressor::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_compression.setTarget(m_store.number(Compression));
    m_gateAtDb = m_store.number(Gate);
    const float ceiling = m_store.number(Ceiling);
    m_ceiling.setTarget(ceiling);
    m_limiter.setCeilingGain(dbToGain(ceiling));

    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // **Both sides read before either is written**, as the buffers may be
        // the same one, and the louder side decides: one gain for both, so the
        // picture does not move.
        std::array<float, kChannels> samples{};
        for (std::uint32_t channel = 0; channel < std::min(channels, kChannels); ++channel) {
            const std::span<const float> in = context.input.channel(channel);
            const float sample = frame < in.size() ? in[frame] : 0.0f;
            // **A bad sample goes no further.** The follower would hold it for
            // good, and the block would stay broken long after the sound was fine.
            samples[channel] = std::isfinite(sample) ? std::clamp(sample, -kLargestSample, kLargestSample) : 0.0f;
        }
        const float gain = gainFor(samples);
        for (std::uint32_t channel = 0; channel < std::min(channels, kChannels); ++channel) {
            const std::span<float> out = context.output.channel(channel);
            if (frame < out.size()) {
                out[frame] = samples[channel] * gain;
            }
        }
    }
    m_limiter.process(context.output, context.frames);
    publish();
}

void VoiceCompressor::publish() noexcept RB_NONBLOCKING
{
    if (m_meter == nullptr) {
        return;
    }
    m_meter->publish(kCompressedValue, -m_heldCompressedDb);
    m_meter->publish(kGatedValue, -m_heldGatedDb);
    m_meter->publish(kLimitedValue, -gainToDb(m_limiter.lowestGain()));
    m_meter->publish(kInputValue, m_levelDb);
}

void VoiceCompressor::reset() noexcept RB_NONBLOCKING
{
    const float compression = m_store.number(Compression);
    const float ceiling = m_store.number(Ceiling);
    m_compression.reset(compression);
    m_ceiling.reset(ceiling);
    m_gateAtDb = m_store.number(Gate);
    m_limiter.setCeilingGain(dbToGain(ceiling));
    m_limiter.reset();
    m_meanSquares = {0.0f, 0.0f};
    m_levelDb = kSilenceDb;
    m_quickDb = 0.0f;
    m_slowDb = 0.0f;
    m_gateDb = 0.0f;
    m_gateHold = 0;
    m_makeupDb = makeupDb(compression, ceiling);
    m_heldCompressedDb = 0.0f;
    m_heldGatedDb = 0.0f;
    m_heldFor = 0;
}

void VoiceCompressor::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> VoiceCompressor::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue VoiceCompressor::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap VoiceCompressor::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> VoiceCompressor::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

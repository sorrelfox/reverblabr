// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/voiceforward.h"

#include "dsp/decibel.h"

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
constexpr std::uint32_t kLeft = 0;
constexpr std::uint32_t kRight = 1;

/// How fast a lift may move, in dB a second: the 3 s smoothing of a 6 dB step
/// asks for 2 at the most, so this only spreads each reading's step out.
constexpr float kLiftGlideDbPerSecond = 2.5f;
/// How long the "taken out now" reading is smoothed over.
constexpr float kTakenOutSmoothingMs = 300.0f;
/// Under the ratio of the two powers, so that silence is a share of nothing.
constexpr float kEmpty = 1.0e-20f;

/// A gain in dB as a factor, cheaply: this runs every sample of a glide.
[[nodiscard]] float gainOf(float db) noexcept RB_NONBLOCKING
{
    return std::exp2(db * (0.05f * 3.321928f));
}

[[nodiscard]] float keepOver(float seconds, float stepSeconds) noexcept
{
    return std::exp(-stepSeconds / seconds);
}

} // namespace

std::vector<ParamDescriptor> VoiceForward::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    const auto cut = [](const char *id, float value) {
        ParamDescriptor descriptor;
        descriptor.id = id;
        descriptor.type = ParamType::Float;
        descriptor.minimum = ParamValue::ofFloat(kLowestCutDb);
        descriptor.maximum = ParamValue::ofFloat(0.0f);
        descriptor.defaultValue = ParamValue::ofFloat(value);
        descriptor.unit = ParamUnit::Decibels;
        return descriptor;
    };
    descriptors.push_back(cut("background", 0.0f));
    descriptors.push_back(cut("betweenVoices", kDefaultBetweenDb));

    ParamDescriptor hold;
    hold.id = "holdLevel";
    hold.type = ParamType::Bool;
    hold.defaultValue = ParamValue::ofBool(true);
    descriptors.push_back(std::move(hold));
    return descriptors;
}

VoiceForward::VoiceForward()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    // GainReach::Adds, the default: nothing shows that taking out the rest
    // cannot raise a sample, and the lift does raise them.
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = static_cast<std::uint32_t>(CentreSplit::kFrameSize);
}

std::unique_ptr<Block> VoiceForward::make()
{
    return std::make_unique<VoiceForward>();
}

std::expected<void, BlockError> VoiceForward::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_split.prepare(spec.sampleRate);
    m_loudnessIn.prepare(spec.sampleRate);
    m_loudnessOut.prepare(spec.sampleRate);
    m_limiter.prepare(spec.sampleRate, kChannels, kCeilingDb);
    m_limiter.setRiseDbPerSecond(kRiseDbPerSecond);
    m_limiter.setMostReleaseDbPerSecond(kRiseDbPerSecond);
    m_info.latencySamples = static_cast<std::uint32_t>(m_split.frameSize()) + m_limiter.latencySamples();

    const auto rate = static_cast<float>(spec.sampleRate);
    m_hopSeconds = static_cast<float>(m_split.hop()) / rate;
    m_shareKeep = keepOver(kShareSmoothingMs * 1.0e-3f, m_hopSeconds);
    m_takenKeep = keepOver(kTakenOutSmoothingMs * 1.0e-3f, m_hopSeconds);
    m_liftKeep = keepOver(kLiftSmoothingSeconds, 0.1f);
    m_fallStepDb = kFallDbPerSecond / rate;
    m_riseStepDb = kRiseDbPerSecond / rate;
    m_liftStepDb = kLiftGlideDbPerSecond / rate;
    m_quietPower = std::pow(10.0f, kQuietDb * 0.1f);
    m_ceilingWaitSamples = static_cast<std::uint32_t>(
        std::ceil(static_cast<double>(LookaheadLimiter::kCeilingGlideMs) * 1.0e-3 * spec.sampleRate));
    m_meter = spec.meter;
    reset();
    return {};
}

float VoiceForward::backgroundDb() const noexcept RB_NONBLOCKING
{
    return std::clamp(m_store.number(Background), kLowestCutDb, 0.0f);
}

float VoiceForward::takenOutDb() const noexcept RB_NONBLOCKING
{
    if (!(m_inPower > kEmpty)) {
        return 0.0f;
    }
    return 10.0f * std::log10(std::max(m_outPower, kEmpty) / m_inPower);
}

float VoiceForward::centreShare() const noexcept RB_NONBLOCKING
{
    return m_centrePower / (m_widePower + kEmpty);
}

void VoiceForward::hopEnded() noexcept RB_NONBLOCKING
{
    const float wideNow = m_split.widePower();
    m_centrePower = m_shareKeep * m_centrePower + (1.0f - m_shareKeep) * m_split.centrePower();
    m_widePower = m_shareKeep * m_widePower + (1.0f - m_shareKeep) * wideNow;
    const float share = centreShare();

    const float hop = static_cast<float>(m_split.hop());
    m_inPower = m_takenKeep * m_inPower + (1.0f - m_takenKeep) * m_inHop / hop;
    m_outPower = m_takenKeep * m_outPower + (1.0f - m_takenKeep) * m_outHop / hop;
    m_inHop = 0.0f;
    m_outHop = 0.0f;

    m_holdSeconds = std::max(0.0f, m_holdSeconds - m_hopSeconds);
    if (wideNow < m_quietPower) {
        // Quiet keeps the last choice of cut: a pause is no change of scene.
        m_found = Quiet;
    } else {
        m_alikeSeconds = share >= kAlikeFrom ? std::min(m_alikeSeconds + m_hopSeconds, kAlikeSeconds) : 0.0f;
        if (share >= kCentreFrom) {
            m_wide = false;
            m_holdSeconds = kCentreHoldMs * 1.0e-3f;
        } else if (share < kWideUnder && m_holdSeconds <= 0.0f) {
            m_wide = true;
        }
        if (m_alikeSeconds >= kAlikeSeconds) {
            m_found = Alike;
            m_wide = false;
        } else {
            m_found = m_wide ? Wide : Centre;
        }
    }

    const float background = backgroundDb();
    const float between = std::clamp(m_store.number(BetweenVoices), kLowestCutDb, 0.0f);
    m_wantedCutDb = m_wide ? std::max(between, background) : background;

    // The ceiling goes up only once nothing is left to limit, and comes down
    // before anything moves.
    const bool nothingToDo = background == 0.0f && m_cutDb == 0.0f && m_liftDb == 0.0f && m_wantedLiftDb == 0.0f;
    if (nothingToDo != m_ceilingOpen) {
        m_ceilingOpen = nothingToDo;
        m_limiter.setCeilingGain(dbToGain(m_ceilingOpen ? kOutOfReachDb : kCeilingDb));
        m_ceilingWait = m_ceilingOpen ? 0 : m_ceilingWaitSamples;
    }

    m_limiterRising = m_limiter.gain() > m_limiterGain;
    m_limiterGain = m_limiter.gain();
}

void VoiceForward::readingsEnded() noexcept RB_NONBLOCKING
{
    m_readings = std::min(m_readings + 1, kReadingsToFill);
    if (m_store.number(HoldLevel) == 0.0f) {
        m_liftReading *= m_liftKeep;
    } else if (m_readings >= kReadingsToFill && m_loudnessIn.loudness() > kSilentLufs
               && m_loudnessOut.loudness() > kSilentLufs) {
        // With either side gated out the last lift stands.
        const float gap = m_loudnessIn.loudness() - m_loudnessOut.loudness();
        m_liftReading = m_liftKeep * m_liftReading + (1.0f - m_liftKeep) * gap;
    }
    const float wanted = std::clamp(m_liftReading, 0.0f, kMostLiftDb);
    m_wantedLiftDb = wanted < kLeastLiftDb ? 0.0f : wanted;
}

void VoiceForward::limit(const ProcessContext &context,
                         std::uint32_t start,
                         std::uint32_t count) noexcept RB_NONBLOCKING
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

void VoiceForward::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::span<float> leftIn = context.input.channel(kLeft);
    const std::span<float> rightIn = context.input.channel(kRight);
    const std::span<float> leftOut = context.output.channel(kLeft);
    const std::span<float> rightOut = context.output.channel(kRight);

    m_limitedDb = 0.0f;
    std::uint32_t limitedTo = 0;
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // Both inputs read before either output is written: the graph may
        // hand a block one buffer to read and to write.
        const float left = frame < leftIn.size() ? leftIn[frame] : 0.0f;
        const float right = frame < rightIn.size() ? rightIn[frame] : 0.0f;
        const CentreSplit::Parts parts = m_split.push(left, right);
        const bool hop = m_split.hopEnded();

        if (m_ceilingWait > 0) {
            --m_ceilingWait;
        } else if (m_wantedCutDb < m_cutDb) {
            m_cutDb = std::max(m_wantedCutDb, m_cutDb - m_fallStepDb);
        } else if (m_wantedCutDb > m_cutDb && !m_limiterRising) {
            m_cutDb = std::min(m_wantedCutDb, m_cutDb + m_riseStepDb);
        }
        if (m_cutDb != m_keepAtDb) {
            m_keepAtDb = m_cutDb;
            m_keep = gainOf(m_cutDb);
        }

        // The whole sound less the part of the rest going: at a gain of 1
        // nothing is subtracted, so the input comes out to the bit.
        const float taken = 1.0f - m_keep;
        const float keptLeft = parts.left - taken * (parts.left - parts.centre);
        const float keptRight = parts.right - taken * (parts.right - parts.centre);

        m_inHop += 0.5f * (parts.left * parts.left + parts.right * parts.right);
        m_outHop += 0.5f * (keptLeft * keptLeft + keptRight * keptRight);
        const bool readIn = m_loudnessIn.push(parts.left, parts.right);
        const bool readOut = m_loudnessOut.push(keptLeft, keptRight);
        if (readIn && readOut) {
            readingsEnded();
        }

        if (m_wantedLiftDb > m_liftDb) {
            if (m_cutDb == m_wantedCutDb && !m_limiterRising) {
                m_liftDb = std::min(m_wantedLiftDb, m_liftDb + m_liftStepDb);
            }
        } else if (m_wantedLiftDb < m_liftDb) {
            m_liftDb = std::max(m_wantedLiftDb, m_liftDb - m_liftStepDb);
        }
        if (m_liftDb != m_liftGainAtDb) {
            m_liftGainAtDb = m_liftDb;
            m_liftGain = gainOf(m_liftDb);
        }

        if (frame < leftOut.size()) {
            leftOut[frame] = keptLeft * m_liftGain;
        }
        if (frame < rightOut.size()) {
            rightOut[frame] = keptRight * m_liftGain;
        }

        // **The limiter runs up to each hop's end, and its ceiling moves
        // there.** Set once a cycle it would move at the cycle, and the sound
        // would depend on the quantum.
        if (hop) {
            limit(context, limitedTo, frame + 1 - limitedTo);
            limitedTo = frame + 1;
            hopEnded();
        }
    }
    limit(context, limitedTo, context.frames - limitedTo);

    if (m_meter != nullptr) {
        m_meter->publish(kTakenOutValue, takenOutDb());
        m_meter->publish(kFoundValue, static_cast<float>(m_found));
        m_meter->publish(kLiftValue, m_liftDb);
        m_meter->publish(kLimitValue, m_limitedDb);
    }
}

void VoiceForward::reset() noexcept RB_NONBLOCKING
{
    m_split.reset();
    m_loudnessIn.reset();
    m_loudnessOut.reset();
    m_centrePower = 0.0f;
    m_widePower = 0.0f;
    m_alikeSeconds = 0.0f;
    m_holdSeconds = 0.0f;
    m_wide = true;
    m_found = Quiet;
    m_wantedCutDb = 0.0f;
    m_cutDb = 0.0f;
    m_keep = 1.0f;
    m_keepAtDb = 0.0f;
    m_readings = 0;
    m_liftReading = 0.0f;
    m_wantedLiftDb = 0.0f;
    m_liftDb = 0.0f;
    m_liftGain = 1.0f;
    m_liftGainAtDb = 0.0f;
    m_ceilingOpen = backgroundDb() == 0.0f;
    m_ceilingWait = 0;
    m_limitedDb = 0.0f;
    m_limiterGain = 1.0f;
    m_limiterRising = false;
    m_inHop = 0.0f;
    m_outHop = 0.0f;
    m_inPower = 0.0f;
    m_outPower = 0.0f;
    // A start takes the ceiling at once: there is no sound yet to be careful of.
    m_limiter.setCeilingGain(dbToGain(m_ceilingOpen ? kOutOfReachDb : kCeilingDb));
    m_limiter.reset();
}

void VoiceForward::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at the next hop, and glided to from there.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> VoiceForward::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue VoiceForward::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap VoiceForward::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> VoiceForward::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

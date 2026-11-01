// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/speechnow.h"

#include "blocks/voicemodel.h"

#include <algorithm>
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

/// Speech Denoise's own hold: a gate's value, never tried for a turn, which
/// is why it can be set.
constexpr float kDefaultHoldMs = 200.0f;
constexpr float kDefaultQuietAgainSeconds = 2.0f;
constexpr float kDefaultShortestTalkMs = 300.0f;

} // namespace

std::vector<ParamDescriptor> SpeechNow::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor hold;
    hold.id = "holdMs";
    hold.type = ParamType::Float;
    hold.minimum = ParamValue::ofFloat(100.0f);
    hold.maximum = ParamValue::ofFloat(2000.0f);
    hold.defaultValue = ParamValue::ofFloat(kDefaultHoldMs);
    hold.unit = ParamUnit::Milliseconds;
    hold.taper = ParamTaper::Logarithmic;
    descriptors.push_back(std::move(hold));

    ParamDescriptor quietAgain;
    quietAgain.id = "quietAgainSeconds";
    quietAgain.type = ParamType::Float;
    quietAgain.minimum = ParamValue::ofFloat(0.0f);
    quietAgain.maximum = ParamValue::ofFloat(10.0f);
    quietAgain.defaultValue = ParamValue::ofFloat(kDefaultQuietAgainSeconds);
    descriptors.push_back(std::move(quietAgain));

    ParamDescriptor shortest;
    shortest.id = "shortestTalkMs";
    shortest.type = ParamType::Float;
    shortest.minimum = ParamValue::ofFloat(0.0f);
    shortest.maximum = ParamValue::ofFloat(2000.0f);
    shortest.defaultValue = ParamValue::ofFloat(kDefaultShortestTalkMs);
    shortest.unit = ParamUnit::Milliseconds;
    descriptors.push_back(std::move(shortest));

    ParamDescriptor detector;
    detector.id = "detector";
    detector.type = ParamType::Enum;
    detector.choices = {"model", "levelAndRhythm"};
    detector.defaultValue = ParamValue::ofEnum("model");
    descriptors.push_back(std::move(detector));

    // Where a thing is shown is no part of how it hears, so a preset leaves
    // these two as they are: a window must not appear because of one.
    ParamDescriptor floating;
    floating.id = "showFloating";
    floating.type = ParamType::Bool;
    floating.defaultValue = ParamValue::ofBool(false);
    floating.flags.inPresets = false;
    descriptors.push_back(std::move(floating));

    ParamDescriptor tray;
    tray.id = "showInTray";
    tray.type = ParamType::Bool;
    tray.defaultValue = ParamValue::ofBool(false);
    tray.flags.inPresets = false;
    descriptors.push_back(std::move(tray));
    return descriptors;
}

SpeechNow::SpeechNow()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Meters;
    m_info.gainReach = GainReach::Unity;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> SpeechNow::make()
{
    return std::make_unique<SpeechNow>();
}

std::expected<void, BlockError> SpeechNow::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_voice.prepare(spec.sampleRate, makeVoiceModel());
    m_meter = spec.meter;
    reset();
    return {};
}

VoiceDetector SpeechNow::preferred() const noexcept RB_NONBLOCKING
{
    return m_store.number(Detector) == 0.0 ? VoiceDetector::Model : VoiceDetector::LevelAndRhythm;
}

void SpeechNow::step(bool voiced) noexcept RB_NONBLOCKING
{
    if (m_state == kSpeaking) {
        m_talkMs += kFrameMs;
        m_gapMs = voiced ? 0 : m_gapMs + kFrameMs;
        if (static_cast<double>(m_gapMs) < m_store.number(HoldMs)) {
            return;
        }
        const std::uint32_t spoken = m_talkMs - m_gapMs;
        const bool marked =
            static_cast<double>(spoken) >= m_store.number(ShortestTalkMs) && m_store.number(QuietAgainSeconds) > 0.0;
        m_state = marked ? kQuietAgain : kQuiet;
        m_run = 0;
        m_markMs = 0;
        return;
    }
    m_run = voiced ? m_run + 1 : 0;
    if (m_state == kQuietAgain) {
        m_markMs += kFrameMs;
        if (static_cast<double>(m_markMs) >= m_store.number(QuietAgainSeconds) * 1000.0) {
            m_state = kQuiet;
        }
    }
    if (m_run >= kOpenFrames) {
        m_state = kSpeaking;
        m_talkMs = kOpenFrames * kFrameMs;
        m_gapMs = 0;
    }
}

void SpeechNow::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_voice.setPreferred(preferred());
    const bool stereoIn = context.input.channelCount() >= kChannels;
    const bool stereoOut = context.output.channelCount() >= kChannels;
    const std::span<float> inLeft = context.input.channel(kLeft);
    const std::span<float> inRight = stereoIn ? context.input.channel(kRight) : inLeft;
    const std::span<float> outLeft = context.output.channel(kLeft);
    const std::span<float> outRight = stereoOut ? context.output.channel(kRight) : outLeft;
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // Both read before either is written: the buffers can be shared.
        const float left = inLeft[frame];
        const float right = inRight[frame];
        outLeft[frame] = left;
        outRight[frame] = right;
        if (m_voice.push(0.5f * (left + right))) {
            step(m_voice.voiced());
        }
    }
    if (m_meter != nullptr) {
        m_meter->publish(kStateValue, m_state);
        m_meter->publish(kVoiceValue, m_voice.likelihood());
        m_meter->publish(kDetectorValue, detectorReading());
    }
}

float SpeechNow::detectorReading() const noexcept RB_NONBLOCKING
{
    return detectorReadingOf(m_voice.modelState(), preferred());
}

float SpeechNow::detectorReadingOf(VoiceModelState model, VoiceDetector preferred) noexcept RB_NONBLOCKING
{
    switch (model) {
    case VoiceModelState::NotInThisBuild:
        return kNoModelInThisBuild;
    case VoiceModelState::NeedsModelRate:
        return kModelNeedsItsRate;
    case VoiceModelState::Ready:
        break;
    }
    return preferred == VoiceDetector::Model ? kModelListening : kLevelAndRhythmChosen;
}

void SpeechNow::reset() noexcept RB_NONBLOCKING
{
    m_voice.setPreferred(preferred());
    m_voice.reset();
    m_state = kQuiet;
    m_run = 0;
    m_talkMs = 0;
    m_gapMs = 0;
    m_markMs = 0;
}

void SpeechNow::switchedOn() noexcept RB_NONBLOCKING
{
    // What it was saying when it was switched off is long out of date.
    reset();
}

void SpeechNow::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> SpeechNow::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue SpeechNow::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap SpeechNow::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> SpeechNow::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/voicegauge.h"

#include "blocks/speechnow.h"
#include "blocks/voicemodel.h"

#include <algorithm>
#include <cmath>
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

constexpr float kDefaultRoomLu = 3.0f;
constexpr float kLevelWindowMs = 100.0f;

[[nodiscard]] bool isSet(float mark) noexcept RB_NONBLOCKING
{
    return mark > MarkPosition::kNotSet;
}

[[nodiscard]] ParamDescriptor numberParam(const char *id, float low, float high, float fallback)
{
    ParamDescriptor descriptor;
    descriptor.id = id;
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(low);
    descriptor.maximum = ParamValue::ofFloat(high);
    descriptor.defaultValue = ParamValue::ofFloat(fallback);
    return descriptor;
}

[[nodiscard]] ParamDescriptor markParam(const char *id)
{
    // A mark is the person's own, so it stays out of a preset: a factory mark
    // would say what a voice should be.
    ParamDescriptor mark = numberParam(id, VoiceGauge::kNotSet, VoiceGauge::kHighestMark, VoiceGauge::kNotSet);
    mark.flags.inPresets = false;
    return mark;
}

} // namespace

MarkPosition::Position MarkPosition::update(float reading, float upper, float lower) noexcept RB_NONBLOCKING
{
    if (reading <= kSilentLufs) {
        m_known = false;
    } else if (!m_known || upper != m_upper || lower != m_lower) {
        m_known = true;
        m_upper = upper;
        m_lower = lower;
        m_overUpper = reading > upper;
        m_overLower = reading > lower;
    } else {
        if (reading >= upper + kPastMarkLu) {
            m_overUpper = true;
        } else if (reading <= upper - kPastMarkLu) {
            m_overUpper = false;
        }
        if (reading >= lower + kPastMarkLu) {
            m_overLower = true;
        } else if (reading <= lower - kPastMarkLu) {
            m_overLower = false;
        }
    }
    const bool hasUpper = isSet(upper);
    const bool hasLower = isSet(lower);
    if (!m_known || (!hasUpper && !hasLower)) {
        return NoMarks;
    }
    if (hasUpper && m_overUpper) {
        return AboveUpper;
    }
    if (hasLower && !m_overLower) {
        return BelowLower;
    }
    if (hasUpper && hasLower) {
        return Between;
    }
    return hasUpper ? BelowUpper : AboveLower;
}

VoiceGauge::Marks VoiceGauge::pushed(float upper, float lower, bool lowerMoved) noexcept RB_NONBLOCKING
{
    Marks marks{std::round(upper), std::round(lower)};
    if (!isSet(marks.upper) || !isSet(marks.lower)) {
        return marks;
    }
    if (lowerMoved) {
        marks.lower = std::min(marks.lower, kHighestMark - kLeastGap);
        marks.upper = std::max(marks.upper, marks.lower + kLeastGap);
    } else {
        marks.upper = std::max(marks.upper, kLowestMark + kLeastGap);
        marks.lower = std::min(marks.lower, marks.upper - kLeastGap);
    }
    return marks;
}

std::vector<ParamDescriptor> VoiceGauge::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(markParam("upperMark"));
    descriptors.push_back(markParam("lowerMark"));

    descriptors.push_back(numberParam("roomEachSide", 1.0f, 9.0f, kDefaultRoomLu));
    descriptors.push_back(numberParam("readingSeconds", 1.0f, 10.0f, VoicedLoudness::kDefaultReadingSeconds));

    ParamDescriptor forget = numberParam("forgetSeconds", 10.0f, 600.0f, VoicedLoudness::kDefaultForgetSeconds);
    forget.taper = ParamTaper::Logarithmic;
    descriptors.push_back(std::move(forget));

    ParamDescriptor detector;
    detector.id = "detector";
    detector.type = ParamType::Enum;
    detector.choices = {"model", "levelAndRhythm"};
    detector.defaultValue = ParamValue::ofEnum("model");
    descriptors.push_back(std::move(detector));

    // Which microphone the marks were placed on is about the marks, and goes
    // where they go.
    ParamDescriptor placedOn;
    placedOn.id = "marksPlacedOn";
    placedOn.type = ParamType::String;
    placedOn.defaultValue = ParamValue::ofString(std::string{});
    placedOn.flags.inPresets = false;
    descriptors.push_back(std::move(placedOn));

    // Where a thing is shown is no part of how it reads, so a preset leaves
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

VoiceGauge::VoiceGauge()
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

std::unique_ptr<Block> VoiceGauge::make()
{
    return std::make_unique<VoiceGauge>();
}

std::expected<void, BlockError> VoiceGauge::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_voice.prepare(spec.sampleRate, makeVoiceModel());
    m_loudness.prepare(spec.sampleRate);
    m_level.prepare(spec.sampleRate, kLevelWindowMs);
    m_meter = spec.meter;
    reset();
    return {};
}

VoiceDetector VoiceGauge::preferred() const noexcept RB_NONBLOCKING
{
    return m_store.number(Detector) == 0.0 ? VoiceDetector::Model : VoiceDetector::LevelAndRhythm;
}

VoiceGauge::Marks VoiceGauge::marksInUse() const noexcept RB_NONBLOCKING
{
    return pushed(
        m_store.number(UpperMark), m_store.number(LowerMark), m_lowerMovedLast.load(std::memory_order_relaxed));
}

void VoiceGauge::followSettings() noexcept RB_NONBLOCKING
{
    m_voice.setPreferred(preferred());
    const float over = m_store.number(ReadingSeconds);
    if (over != m_readingSeconds) {
        m_readingSeconds = over;
        m_loudness.setReadingSeconds(over);
    }
    const float forget = m_store.number(ForgetSeconds);
    if (forget != m_forgetSeconds) {
        m_forgetSeconds = forget;
        m_loudness.setForgetSeconds(forget);
    }
}

void VoiceGauge::closeFrame() noexcept RB_NONBLOCKING
{
    m_sinceVoiceMs = m_voice.voiced() ? 0 : std::min(m_sinceVoiceMs + kFrameMs, kVoiceNowMs);
    m_voiceNow = m_sinceVoiceMs < kVoiceNowMs;
    m_silentMs = m_level.level() < kNoSoundLevel ? std::min(m_silentMs + kFrameMs, kNoSoundMs) : 0;
    if (m_silentMs >= kNoSoundMs) {
        m_sound = NoSound;
    } else {
        m_sound = m_voiceNow ? Voice : RoomOnly;
    }
    const Marks marks = marksInUse();
    m_positionNow = m_position.update(m_loudness.reading(), marks.upper, marks.lower);
}

void VoiceGauge::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    followSettings();
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
        // The louder side and not the fold: two sides in antiphase fold to
        // nothing, and "No sound here" would then be untrue.
        m_level.push(std::abs(left) >= std::abs(right) ? left : right);
        // In this order, so the detector's flag belongs to this very slice.
        const bool frameEnded = m_voice.push(0.5f * (left + right));
        m_loudness.push(left, right, m_voice.voiced());
        if (frameEnded) {
            closeFrame();
        }
    }
    if (m_meter != nullptr) {
        m_meter->publish(kStateValue, stateWord());
        m_meter->publish(kReadingValue, m_loudness.reading());
        m_meter->publish(kWholeLevelValue, m_loudness.wholeLevel());
        m_meter->publish(kMarksValue, marksWord());
    }
}

std::uint32_t VoiceGauge::secondsCounted() const noexcept RB_NONBLOCKING
{
    // A second of voice is ten blocks, after the 300 ms the first one needs
    // over a hop: full, the ring is 197 of them and exactly 20 s.
    return static_cast<std::uint32_t>((m_loudness.blocks() + 3) / 10);
}

float VoiceGauge::stateWord() const noexcept RB_NONBLOCKING
{
    const std::uint32_t detector = static_cast<std::uint32_t>(detectorReading());
    return static_cast<float>(m_sound + (m_voiceNow ? kPerVoiceNow : 0) + m_positionNow * kPerPosition
                              + secondsCounted() * kPerSecond + detector * kPerDetector);
}

float VoiceGauge::marksWord() const noexcept RB_NONBLOCKING
{
    return marksWordOf(marksInUse());
}

float VoiceGauge::marksWordOf(Marks marks) noexcept RB_NONBLOCKING
{
    return (marks.upper - kNotSet) * static_cast<float>(kPerUpperMark) + (marks.lower - kNotSet);
}

float VoiceGauge::detectorReading() const noexcept RB_NONBLOCKING
{
    return SpeechNow::detectorReadingOf(m_voice.modelState(), preferred());
}

void VoiceGauge::reset() noexcept RB_NONBLOCKING
{
    followSettings();
    m_voice.reset();
    m_loudness.reset();
    m_level.reset();
    m_position.reset();
    m_positionNow = MarkPosition::NoMarks;
    m_sound = RoomOnly;
    m_voiceNow = false;
    m_silentMs = 0;
    m_sinceVoiceMs = kVoiceNowMs;
}

void VoiceGauge::switchedOn() noexcept RB_NONBLOCKING
{
    // What it held when it was switched off is long out of date.
    reset();
}

void VoiceGauge::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    if (index == UpperMark || index == LowerMark) {
        m_lowerMovedLast.store(index == LowerMark, std::memory_order_relaxed);
    }
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> VoiceGauge::setParam(std::string_view id, const ParamValue &value)
{
    if (id == "upperMark" || id == "lowerMark") {
        m_lowerMovedLast.store(id == "lowerMark", std::memory_order_relaxed);
    }
    return m_store.set(id, value);
}

ParamValue VoiceGauge::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap VoiceGauge::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> VoiceGauge::loadState(const ParamValueMap &state)
{
    m_lowerMovedLast.store(false, std::memory_order_relaxed);
    return m_store.load(state);
}

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/oneearmix/oneearmix.h"

#include "dsp/decibel.h"

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

constexpr std::uint32_t kNoChange = 0;
constexpr std::uint32_t kToLeft = 1;
constexpr std::uint32_t kToRight = 2;

constexpr std::size_t kLeftFromLeft = 0;
constexpr std::size_t kLeftFromRight = 1;
constexpr std::size_t kRightFromLeft = 2;
constexpr std::size_t kRightFromRight = 3;

} // namespace

void OneEarMix::Weight::reset(float value) noexcept RB_NONBLOCKING
{
    m_value = value;
    m_target = value;
    m_fallStep = 0.0;
    m_fromSilence = false;
}

void OneEarMix::Weight::restartRise() noexcept RB_NONBLOCKING
{
    if (m_target > 0.0) {
        m_value = std::min(m_value, kRiseFrom);
        m_fromSilence = true;
    }
}

void OneEarMix::Weight::setTarget(float target, std::uint32_t fallSamples) noexcept RB_NONBLOCKING
{
    const double wanted = target;
    if (wanted == m_target) {
        return;
    }
    m_target = wanted;
    if (wanted < m_value) {
        m_fallStep = (m_value - wanted) / static_cast<double>(fallSamples);
        m_fromSilence = false;
    } else if (m_value <= kRiseFrom) {
        m_fromSilence = true;
    }
}

std::vector<ParamDescriptor> OneEarMix::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor sendTo;
    sendTo.id = "sendTo";
    sendTo.type = ParamType::Enum;
    sendTo.choices = {"none", "left", "right", "both"};
    sendTo.defaultValue = ParamValue::ofEnum("none");
    descriptors.push_back(std::move(sendTo));

    ParamDescriptor hear;
    hear.id = "hear";
    hear.type = ParamType::Enum;
    hear.choices = {"fold", "mono"};
    hear.defaultValue = ParamValue::ofEnum("fold");
    descriptors.push_back(std::move(hear));

    ParamDescriptor lower;
    lower.id = "lowerDb";
    lower.type = ParamType::Float;
    lower.minimum = ParamValue::ofFloat(0.0f);
    lower.maximum = ParamValue::ofFloat(kMostLoweredDb);
    lower.defaultValue = ParamValue::ofFloat(0.0f);
    lower.unit = ParamUnit::Decibels;
    lower.smoothingMs = kFallMs;
    descriptors.push_back(std::move(lower));

    // **Asked of a running block, and of nothing else.** A reading is not a
    // setting: it has no place in a file, in undo or in a preset.
    ParamDescriptor match;
    match.id = "matchLevels";
    match.type = ParamType::Bool;
    match.defaultValue = ParamValue::ofBool(false);
    match.flags.hidden = true;
    match.flags.inPresets = false;
    match.flags.momentary = true;
    descriptors.push_back(std::move(match));

    ParamDescriptor output;
    output.id = "newOutput";
    output.type = ParamType::Bool;
    output.defaultValue = ParamValue::ofBool(false);
    output.flags.hidden = true;
    output.flags.inPresets = false;
    output.flags.momentary = true;
    output.flags.pressedByNewOutput = true;
    descriptors.push_back(std::move(output));
    return descriptors;
}

OneEarMix::OneEarMix()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Hearing;
    // GainReach::Adds, the default, is the right word: see the class comment.
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    m_info.latencySamples = 0;
    m_weights[kLeftFromLeft].reset(1.0f);
    m_weights[kRightFromRight].reset(1.0f);
}

std::unique_ptr<Block> OneEarMix::make()
{
    return std::make_unique<OneEarMix>();
}

OneEarMix::Route OneEarMix::wantedRoute() const noexcept RB_NONBLOCKING
{
    const auto choice = static_cast<std::uint32_t>(m_store.number(SendTo));
    if (choice == kNoChange) {
        return {{1.0f, 0.0f, 0.0f, 1.0f}, 0.0f};
    }
    const bool plain = m_store.number(Hear) != 0.0;
    const float each =
        plain ? 0.5f : QuadratureFold::kFoldGain * dbToGain(-static_cast<float>(m_store.number(LowerDb)));
    const float filtered = plain ? 0.0f : 1.0f;
    const float toLeft = choice != kToRight ? each : 0.0f;
    const float toRight = choice != kToLeft ? each : 0.0f;
    return {{toLeft, toLeft, toRight, toRight}, filtered};
}

std::array<float, 4> OneEarMix::weights() const noexcept RB_NONBLOCKING
{
    return {m_weights[0].value(), m_weights[1].value(), m_weights[2].value(), m_weights[3].value()};
}

void OneEarMix::start() noexcept RB_NONBLOCKING
{
    const Route route = wantedRoute();
    for (std::size_t index = 0; index < m_weights.size(); ++index) {
        m_weights[index].reset(route.weights[index]);
    }
    m_weights[kLeftFromRight].restartRise();
    m_weights[kRightFromLeft].restartRise();
    m_filtered.reset(route.filtered);
    m_fold.reset();
    m_filtersOn = false;
    m_readingAsked = false;
    m_readingLeft = 0;
    m_readingStage = kNotRead;
}

std::expected<void, BlockError> OneEarMix::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_meter = spec.meter;
    m_fold.prepare(spec.sampleRate);
    m_filtered.prepare(spec.sampleRate, kFallMs);
    m_fallSamples =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(kFallMs * 1.0e-3 * spec.sampleRate)));
    m_risePerSample = std::pow(10.0, static_cast<double>(kRiseDbPerSecond) / 20.0 / spec.sampleRate);
    m_readingLength = static_cast<std::uint32_t>(std::lround(kReadingMs * 1.0e-3 * spec.sampleRate));
    m_foldLoudness.prepare(spec.sampleRate);
    m_monoLoudness.prepare(spec.sampleRate);
    start();
    m_prepared = true;
    return {};
}

void OneEarMix::startReading() noexcept RB_NONBLOCKING
{
    m_readingAsked = false;
    m_foldLoudness.reset();
    m_monoLoudness.reset();
    m_readingLeft = m_readingLength;
    m_readingStage = kReading;
}

void OneEarMix::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const Route route = wantedRoute();
    bool settled = true;
    for (std::size_t index = 0; index < m_weights.size(); ++index) {
        m_weights[index].setTarget(route.weights[index], m_fallSamples);
        settled = settled && m_weights[index].settled();
    }
    m_filtered.setTarget(route.filtered);
    if (m_readingAsked) {
        startReading();
    }

    const bool filtersWanted = m_filtered.current() > 0.0f || route.filtered > 0.0f || m_readingLeft > 0;
    if (filtersWanted && !m_filtersOn) {
        m_fold.reset();
    }
    m_filtersOn = filtersWanted;
    // **Left alone, bit for bit.** Nothing is multiplied, so nothing rounds.
    const bool passes = !filtersWanted && settled && route.weights == std::array{1.0f, 0.0f, 0.0f, 1.0f};

    const std::span<float> leftIn = context.input.channel(kLeft);
    const std::span<float> rightIn = context.input.channel(kRight);
    const std::span<float> leftOut = context.output.channel(kLeft);
    const std::span<float> rightOut = context.output.channel(kRight);
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // Both inputs are read before either output is written: the graph may
        // hand a block the same buffer to read and to write.
        const float left = frame < leftIn.size() ? leftIn[frame] : 0.0f;
        const float right = frame < rightIn.size() ? rightIn[frame] : 0.0f;
        float outLeft = left;
        float outRight = right;
        if (!passes) {
            const QuadratureFold::Frame fold = m_filtersOn ? m_fold.process(left, right) : QuadratureFold::Frame{};
            const float filtered = m_filtered.next();
            const float sideLeft = (1.0f - filtered) * left + filtered * fold.a;
            const float sideRight = (1.0f - filtered) * right + filtered * fold.b;
            outLeft = m_weights[kLeftFromLeft].next(m_risePerSample) * sideLeft
                + m_weights[kLeftFromRight].next(m_risePerSample) * sideRight;
            outRight = m_weights[kRightFromLeft].next(m_risePerSample) * sideLeft
                + m_weights[kRightFromRight].next(m_risePerSample) * sideRight;
            if (m_readingLeft > 0) {
                // Both on the left, so that both carry the same offset.
                (void)m_foldLoudness.push(QuadratureFold::kFoldGain * (fold.a + fold.b), 0.0f);
                (void)m_monoLoudness.push(0.5f * (left + right), 0.0f);
                if (--m_readingLeft == 0) {
                    m_readingStage = kRead;
                }
            }
        }
        if (frame < leftOut.size()) {
            leftOut[frame] = outLeft;
        }
        if (frame < rightOut.size()) {
            rightOut[frame] = outRight;
        }
    }

    if (m_meter != nullptr) {
        const bool arriving = std::ranges::any_of(m_weights, [](const Weight &weight) { return weight.arriving(); });
        m_meter->publish(kStateValue, arriving ? kArriving : kSettled);
        m_meter->publish(kReadingValue, m_readingStage);
        if (m_readingStage != kNotRead) {
            m_meter->publish(kFoldLoudnessValue, m_foldLoudness.loudness());
            m_meter->publish(kMonoLoudnessValue, m_monoLoudness.loudness());
        }
    }
}

void OneEarMix::reset() noexcept RB_NONBLOCKING
{
    start();
}

void OneEarMix::switchedOn() noexcept RB_NONBLOCKING
{
    m_weights[kLeftFromRight].restartRise();
    m_weights[kRightFromLeft].restartRise();
    m_fold.reset();
}

void OneEarMix::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    if (index == MatchLevels) {
        m_readingAsked = true;
        return;
    }
    if (index == NewOutput) {
        m_weights[kLeftFromRight].restartRise();
        m_weights[kRightFromLeft].restartRise();
        return;
    }
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> OneEarMix::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue OneEarMix::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap OneEarMix::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> OneEarMix::loadState(const ParamValueMap &state)
{
    // No jump to what was loaded: process() glides there, by the same law.
    return m_store.load(state);
}

} // namespace rb::blocks

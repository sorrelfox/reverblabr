// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/frequencymoveblock.h"

#include "rt/meter.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::array<std::string_view, FrequencyMoveBlock::MoveParamCount> kMoveParamNames{
    "Used",
    "On",
    "Method",
    "SourceLow",
    "SourceHigh",
    "DestinationLow",
    "DestinationHigh",
    "Level",
    "Source",
    "When",
};

[[nodiscard]] ParamDescriptor frequency(const FrequencyMoveKind &kind, std::string id, float value)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(kind.minimumHz);
    descriptor.maximum = ParamValue::ofFloat(kind.maximumHz);
    descriptor.defaultValue = ParamValue::ofFloat(value);
    descriptor.unit = ParamUnit::Hertz;
    descriptor.taper = ParamTaper::Logarithmic;
    return descriptor;
}

[[nodiscard]] ParamDescriptor choice(std::string id, std::span<const std::string_view> ids, std::size_t initial)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Enum;
    for (const std::string_view each : ids) {
        descriptor.choices.emplace_back(each);
    }
    descriptor.defaultValue = ParamValue::ofEnum(std::string{ids[initial]});
    return descriptor;
}

[[nodiscard]] ParamDescriptor toggle(std::string id, bool value)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Bool;
    descriptor.defaultValue = ParamValue::ofBool(value);
    return descriptor;
}

} // namespace

std::string FrequencyMoveBlock::paramId(std::size_t move, MoveParam which)
{
    return "move" + std::to_string(move + 1) + std::string{kMoveParamNames[which]};
}

std::vector<ParamDescriptor> FrequencyMoveBlock::makeDescriptors(const FrequencyMoveKind &kind)
{
    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(earParam());
    ParamDescriptor movedOnly = toggle("movedOnly", false);
    // A way of listening while setting a level, not part of how it sounds.
    movedOnly.flags.inPresets = false;
    descriptors.push_back(std::move(movedOnly));

    for (std::size_t move = 0; move < kMoveCount; ++move) {
        const MoveRanges &defaults = kind.defaults[move];
        descriptors.push_back(toggle(paramId(move, Used), move == 0));
        descriptors.push_back(toggle(paramId(move, On), true));
        descriptors.push_back(choice(paramId(move, Method), kind.methodIds, kind.defaultMethod));
        descriptors.push_back(frequency(kind, paramId(move, SourceLow), defaults.sourceLow));
        descriptors.push_back(frequency(kind, paramId(move, SourceHigh), defaults.sourceHigh));
        descriptors.push_back(frequency(kind, paramId(move, DestinationLow), defaults.destinationLow));
        descriptors.push_back(frequency(kind, paramId(move, DestinationHigh), defaults.destinationHigh));

        ParamDescriptor level;
        level.id = paramId(move, Level);
        level.type = ParamType::Float;
        level.minimum = ParamValue::ofFloat(kMinimumLevelDb);
        level.maximum = ParamValue::ofFloat(kMaximumLevelDb);
        level.defaultValue = ParamValue::ofFloat(0.0f);
        level.unit = ParamUnit::Decibels;
        descriptors.push_back(std::move(level));

        descriptors.push_back(choice(paramId(move, Source), kSourceIds, kind.defaultSource));
        descriptors.push_back(choice(paramId(move, When), kWhenIds, 0));
    }
    return descriptors;
}

FrequencyMoveBlock::FrequencyMoveBlock(const FrequencyMoveKind &kind)
    : m_kind(kind)
    , m_descriptors(makeDescriptors(kind))
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kind.typeId};
    m_info.category = BlockCategory::Hearing;
    m_info.inputs = PortSpec::fixed(2, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // The mover's frame, which follows the rate: set in prepare().
    m_info.latencySamples = 0;
}

FrequencyMove FrequencyMoveBlock::moveSettings(std::size_t move) const noexcept RB_NONBLOCKING
{
    const auto number = [this, move](MoveParam which) { return m_store.number(paramIndex(move, which)); };
    FrequencyMove settings;
    settings.active = number(Used) >= 0.5f && number(On) >= 0.5f;
    // The stored number counts the kind's methods, which are MoveMethod's
    // in its order.
    const long method = std::clamp(std::lround(number(Method)), 0L, static_cast<long>(m_kind.methodIds.size()) - 1);
    settings.method = static_cast<MoveMethod>(method);
    settings.sourceLowHz = number(SourceLow);
    settings.sourceHighHz = number(SourceHigh);
    settings.destinationLowHz = number(DestinationLow);
    settings.destinationHighHz = number(DestinationHigh);
    settings.levelDb = number(Level);
    const auto source = static_cast<int>(std::lround(number(Source)));
    settings.source = source == 0 ? MoveSource::Keep : (source == 1 ? MoveSource::Lower : MoveSource::Remove);
    settings.when = number(When) >= 0.5f ? MoveWhen::WhenNeeded : MoveWhen::Always;
    // **The destination on the kind's side of the source**, whatever order
    // the numbers came in: a range given upside down is the same range, and
    // a destination on the other side is not this block's move. The panel
    // keeps them so; a session written by hand may not.
    if (settings.sourceLowHz > settings.sourceHighHz) {
        std::swap(settings.sourceLowHz, settings.sourceHighHz);
    }
    if (settings.destinationLowHz > settings.destinationHighHz) {
        std::swap(settings.destinationLowHz, settings.destinationHighHz);
    }
    const bool onItsSide = m_kind.raises ? settings.destinationHighHz > settings.sourceHighHz
                                         : settings.destinationLowHz < settings.sourceLowHz;
    if (!onItsSide) {
        settings.active = false;
    }
    return settings;
}

void FrequencyMoveBlock::takeSettings() noexcept RB_NONBLOCKING
{
    for (std::size_t move = 0; move < kMoveCount; ++move) {
        m_mover.setMove(move, moveSettings(move));
    }
    m_mover.setMovedOnly(m_store.number(MovedOnly) >= 0.5f);
}

std::expected<void, BlockError> FrequencyMoveBlock::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_meter = spec.meter;
    m_mover.prepare(spec.sampleRate, m_kind.frameMs);
    m_info.latencySamples = m_mover.latencySamples();
    m_router.prepare(spec.sampleRate, spec.maxBlockFrames, m_info.latencySamples, earFromNumber(m_store.number(Ear)));
    takeSettings();
    reset();
    return {};
}

void FrequencyMoveBlock::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    takeSettings();
    m_router.setEar(earFromNumber(m_store.number(Ear)));
    m_router.process(
        context,
        [this](std::span<float> samples) noexcept RB_NONBLOCKING { m_mover.process(samples, samples); },
        [this]() noexcept RB_NONBLOCKING { m_mover.reset(); });

    std::array<float, kMoveCount> acting{};
    std::uint32_t bits = 0;
    for (std::size_t move = 0; move < kMoveCount; ++move) {
        if (m_mover.acting(move)) {
            acting[move] = 1.0f;
            bits |= 1U << move;
        }
    }
    if (m_meter != nullptr) {
        m_meter->publish(kActingValue, static_cast<float>(bits));
        m_meter->publishBands(acting);
    }
}

void FrequencyMoveBlock::reset() noexcept RB_NONBLOCKING
{
    m_router.reset();
    m_mover.reset();
}

void FrequencyMoveBlock::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> FrequencyMoveBlock::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue FrequencyMoveBlock::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap FrequencyMoveBlock::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> FrequencyMoveBlock::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

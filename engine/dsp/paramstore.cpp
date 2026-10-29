// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/paramstore.h"

#include <algorithm>
#include <cmath>

namespace rb::dsp
{
namespace
{

[[nodiscard]] bool isTextType(ParamType type) noexcept
{
    return type == ParamType::String || type == ParamType::DeviceRef;
}

[[nodiscard]] bool isListType(ParamType type) noexcept
{
    return type == ParamType::StringList;
}

} // namespace

ParamStore::ParamStore(std::span<const ParamDescriptor> descriptors)
    : m_descriptors(descriptors)
    , m_limits(descriptors.size())
    , m_numbers(descriptors.size())
    , m_texts(descriptors.size())
    , m_lists(descriptors.size())
{
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const ParamDescriptor &descriptor = descriptors[index];
        NumberLimits &limits = m_limits[index];
        limits.type = descriptor.type;
        limits.bounded = !descriptor.minimum.isEmpty();
        if (limits.bounded) {
            limits.minimum = static_cast<float>(*descriptor.minimum.asNumber());
            limits.maximum = static_cast<float>(*descriptor.maximum.asNumber());
        }
        limits.choiceCount = static_cast<std::uint32_t>(descriptor.choices.size());
    }
    resetToDefaults();
}

std::optional<std::uint32_t> ParamStore::indexOf(std::string_view id) const
{
    for (std::size_t index = 0; index < m_descriptors.size(); ++index) {
        if (m_descriptors[index].id == id) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::nullopt;
}

float ParamStore::numberOf(const ParamDescriptor &descriptor, const ParamValue &value)
{
    switch (descriptor.type) {
    case ParamType::Float:
    case ParamType::Int:
        return static_cast<float>(value.asNumber().value_or(0.0));
    case ParamType::Bool:
        return value.asBool().value_or(false) ? 1.0f : 0.0f;
    case ParamType::Enum: {
        const std::string_view chosen = value.asEnum().value_or(std::string_view{});
        const auto found = std::ranges::find(descriptor.choices, chosen);
        return found == descriptor.choices.end() ? 0.0f
                                                 : static_cast<float>(std::distance(descriptor.choices.begin(), found));
    }
    case ParamType::String:
    case ParamType::DeviceRef:
    case ParamType::StringList:
        break;
    }
    return 0.0f;
}

void ParamStore::resetToDefaults()
{
    for (std::size_t index = 0; index < m_descriptors.size(); ++index) {
        const ParamDescriptor &descriptor = m_descriptors[index];
        m_numbers[index].store(numberOf(descriptor, descriptor.defaultValue), std::memory_order_relaxed);
        m_texts[index] =
            isTextType(descriptor.type) ? std::string{descriptor.defaultValue.asText().value_or("")} : std::string{};
        m_lists[index] = isListType(descriptor.type)
            ? descriptor.defaultValue.asStringList().value_or(std::vector<std::string>{})
            : std::vector<std::string>{};
    }
}

void ParamStore::applyNumber(std::uint32_t index, float number) noexcept RB_NONBLOCKING
{
    if (index >= m_numbers.size()) {
        return;
    }
    const NumberLimits &limits = m_limits[index];
    switch (limits.type) {
    case ParamType::Float:
    case ParamType::Int:
        if (limits.bounded) {
            number = std::clamp(number, limits.minimum, limits.maximum);
        }
        if (limits.type == ParamType::Int) {
            number = std::round(number);
        }
        break;
    case ParamType::Bool:
        number = number != 0.0f ? 1.0f : 0.0f;
        break;
    case ParamType::Enum:
        if (limits.choiceCount == 0) {
            return;
        }
        number = std::clamp(std::round(number), 0.0f, static_cast<float>(limits.choiceCount - 1));
        break;
    case ParamType::String:
    case ParamType::DeviceRef:
    case ParamType::StringList:
        // Text is never set from the audio thread: it owns memory.
        return;
    }
    m_numbers[index].store(number, std::memory_order_relaxed);
}

ParamValue ParamStore::value(std::uint32_t index) const
{
    if (index >= m_numbers.size()) {
        return {};
    }
    const ParamDescriptor &descriptor = m_descriptors[index];
    const float number = m_numbers[index].load(std::memory_order_relaxed);
    switch (descriptor.type) {
    case ParamType::Float:
        return ParamValue::ofFloat(number);
    case ParamType::Int:
        return ParamValue::ofInt(static_cast<std::int64_t>(std::llround(number)));
    case ParamType::Bool:
        return ParamValue::ofBool(number != 0.0f);
    case ParamType::Enum: {
        const auto choice = static_cast<std::size_t>(std::llround(number));
        return ParamValue::ofEnum(choice < descriptor.choices.size() ? descriptor.choices[choice] : std::string{});
    }
    case ParamType::String:
        return ParamValue::ofString(m_texts[index]);
    case ParamType::DeviceRef:
        return ParamValue::ofDeviceRef(m_texts[index]);
    case ParamType::StringList:
        return ParamValue::ofStringList(m_lists[index]);
    }
    return {};
}

ParamValue ParamStore::value(std::string_view id) const
{
    const std::optional<std::uint32_t> index = indexOf(id);
    return index ? value(*index) : ParamValue{};
}

std::expected<void, BlockError> ParamStore::setReadOnly(std::uint32_t index, const ParamValue &value)
{
    if (index >= m_numbers.size()) {
        return std::unexpected(BlockError::UnknownParam);
    }
    const ParamDescriptor &descriptor = m_descriptors[index];
    if (!value.is(descriptor.type)) {
        return std::unexpected(BlockError::ParamTypeMismatch);
    }
    if (isListType(descriptor.type)) {
        m_lists[index] = *value.asStringList();
    } else if (isTextType(descriptor.type)) {
        m_texts[index] = std::string{*value.asText()};
    } else {
        m_numbers[index].store(numberOf(descriptor, clampToRange(descriptor, value)), std::memory_order_relaxed);
    }
    return {};
}

std::expected<void, BlockError> ParamStore::set(std::uint32_t index, const ParamValue &value)
{
    if (index >= m_numbers.size()) {
        return std::unexpected(BlockError::UnknownParam);
    }
    const ParamDescriptor &descriptor = m_descriptors[index];
    if (descriptor.flags.readOnly) {
        return std::unexpected(BlockError::ParamReadOnly);
    }
    if (!value.is(descriptor.type)) {
        return std::unexpected(BlockError::ParamTypeMismatch);
    }
    if (!isValueAllowed(descriptor, value)) {
        return std::unexpected(BlockError::ParamOutOfRange);
    }
    if (isListType(descriptor.type)) {
        m_lists[index] = *value.asStringList();
    } else if (isTextType(descriptor.type)) {
        m_texts[index] = std::string{*value.asText()};
    } else {
        m_numbers[index].store(numberOf(descriptor, value), std::memory_order_relaxed);
    }
    return {};
}

std::expected<void, BlockError> ParamStore::set(std::string_view id, const ParamValue &value)
{
    const std::optional<std::uint32_t> index = indexOf(id);
    if (!index) {
        return std::unexpected(BlockError::UnknownParam);
    }
    return set(*index, value);
}

ParamValueMap ParamStore::save() const
{
    ParamValueMap state;
    for (std::size_t index = 0; index < m_descriptors.size(); ++index) {
        if (m_descriptors[index].flags.readOnly) {
            continue;
        }
        state.emplace(m_descriptors[index].id, value(static_cast<std::uint32_t>(index)));
    }
    return state;
}

std::expected<void, BlockError> ParamStore::load(const ParamValueMap &state)
{
    // Everything starts from its default, so that what the state leaves out
    // is the default rather than whatever happened to be there.
    resetToDefaults();
    for (std::size_t index = 0; index < m_descriptors.size(); ++index) {
        const ParamDescriptor &descriptor = m_descriptors[index];
        if (descriptor.flags.readOnly) {
            continue; // a block output is telemetry, never restored
        }
        const ParamValueMap::const_iterator saved = state.find(descriptor.id);
        if (saved == state.end()) {
            continue;
        }
        if (const std::expected<void, BlockError> applied = set(static_cast<std::uint32_t>(index), saved->second);
            !applied) {
            return applied;
        }
    }
    return {};
}

} // namespace rb::dsp

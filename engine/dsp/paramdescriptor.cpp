// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/paramdescriptor.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace rb::dsp
{
namespace
{

/// Whether a type has a numeric range at all.
[[nodiscard]] bool hasRange(ParamType type) noexcept
{
    return type == ParamType::Float || type == ParamType::Int;
}

/// The first thing wrong with the range of a numeric parameter.
[[nodiscard]] std::expected<void, DescriptorError> validateRange(const ParamDescriptor &descriptor)
{
    const bool hasMinimum = !descriptor.minimum.isEmpty();
    const bool hasMaximum = !descriptor.maximum.isEmpty();

    if (!hasRange(descriptor.type)) {
        if (hasMinimum || hasMaximum) {
            return std::unexpected(DescriptorError::RangeOnTypeWithoutOne);
        }
        return {};
    }

    if (hasMinimum != hasMaximum) {
        return std::unexpected(DescriptorError::RangeIncomplete);
    }
    if (!hasMinimum) {
        return {}; // a numeric parameter may be unbounded
    }
    if (!descriptor.minimum.is(descriptor.type) || !descriptor.maximum.is(descriptor.type)) {
        return std::unexpected(DescriptorError::RangeTypeMismatch);
    }

    const double minimum = *descriptor.minimum.asNumber();
    const double maximum = *descriptor.maximum.asNumber();
    if (minimum > maximum) {
        return std::unexpected(DescriptorError::RangeInverted);
    }
    if (descriptor.type == ParamType::Int
        && (std::abs(minimum) > static_cast<double>(kMaxIntParamMagnitude)
            || std::abs(maximum) > static_cast<double>(kMaxIntParamMagnitude))) {
        return std::unexpected(DescriptorError::IntRangeTooWide);
    }
    if (descriptor.taper == ParamTaper::Logarithmic && minimum <= 0.0) {
        return std::unexpected(DescriptorError::LogarithmicRangeNotPositive);
    }

    const double value = *descriptor.defaultValue.asNumber();
    if (value < minimum || value > maximum) {
        return std::unexpected(DescriptorError::DefaultOutOfRange);
    }
    return {};
}

/// The first thing wrong with an enum's choices.
[[nodiscard]] std::expected<void, DescriptorError> validateChoices(const ParamDescriptor &descriptor)
{
    if (descriptor.type != ParamType::Enum) {
        if (!descriptor.choices.empty()) {
            return std::unexpected(DescriptorError::ChoicesOnNonEnum);
        }
        return {};
    }
    if (descriptor.choices.empty()) {
        return std::unexpected(DescriptorError::EnumWithoutChoices);
    }

    std::unordered_set<std::string_view> seen;
    for (const std::string &choice : descriptor.choices) {
        if (choice.empty()) {
            return std::unexpected(DescriptorError::EmptyChoiceId);
        }
        if (!seen.insert(choice).second) {
            return std::unexpected(DescriptorError::DuplicateChoiceId);
        }
    }

    const std::string_view chosen = *descriptor.defaultValue.asEnum();
    if (!seen.contains(chosen)) {
        return std::unexpected(DescriptorError::DefaultNotAChoice);
    }
    return {};
}

} // namespace

std::string_view paramUnitName(ParamUnit unit) noexcept
{
    switch (unit) {
    case ParamUnit::None:
        return "none";
    case ParamUnit::Decibels:
        return "dB";
    case ParamUnit::Percent:
        return "%";
    case ParamUnit::Hertz:
        return "Hz";
    case ParamUnit::Milliseconds:
        return "ms";
    case ParamUnit::Ratio:
        return "ratio";
    case ParamUnit::Samples:
        return "samples";
    case ParamUnit::Semitones:
        return "st";
    case ParamUnit::Cents:
        return "ct";
    }
    return "unknown";
}

std::string_view paramTaperName(ParamTaper taper) noexcept
{
    switch (taper) {
    case ParamTaper::Linear:
        return "linear";
    case ParamTaper::Logarithmic:
        return "log";
    case ParamTaper::Decibels:
        return "dB";
    }
    return "unknown";
}

std::string_view descriptorErrorName(DescriptorError error) noexcept
{
    switch (error) {
    case DescriptorError::EmptyId:
        return "emptyId";
    case DescriptorError::DuplicateId:
        return "duplicateId";
    case DescriptorError::MissingDefault:
        return "missingDefault";
    case DescriptorError::DefaultTypeMismatch:
        return "defaultTypeMismatch";
    case DescriptorError::RangeTypeMismatch:
        return "rangeTypeMismatch";
    case DescriptorError::RangeOnTypeWithoutOne:
        return "rangeOnTypeWithoutOne";
    case DescriptorError::RangeIncomplete:
        return "rangeIncomplete";
    case DescriptorError::RangeInverted:
        return "rangeInverted";
    case DescriptorError::DefaultOutOfRange:
        return "defaultOutOfRange";
    case DescriptorError::IntRangeTooWide:
        return "intRangeTooWide";
    case DescriptorError::LogarithmicRangeNotPositive:
        return "logarithmicRangeNotPositive";
    case DescriptorError::ChoicesOnNonEnum:
        return "choicesOnNonEnum";
    case DescriptorError::EnumWithoutChoices:
        return "enumWithoutChoices";
    case DescriptorError::EmptyChoiceId:
        return "emptyChoiceId";
    case DescriptorError::DuplicateChoiceId:
        return "duplicateChoiceId";
    case DescriptorError::DefaultNotAChoice:
        return "defaultNotAChoice";
    case DescriptorError::NegativeSmoothing:
        return "negativeSmoothing";
    case DescriptorError::SmoothingOnUnsmoothableType:
        return "smoothingOnUnsmoothableType";
    case DescriptorError::ReadOnlyAndAutomatable:
        return "readOnlyAndAutomatable";
    }
    return "unknown";
}

std::expected<void, DescriptorError> validateParam(const ParamDescriptor &descriptor)
{
    if (descriptor.id.empty()) {
        return std::unexpected(DescriptorError::EmptyId);
    }
    if (descriptor.defaultValue.isEmpty()) {
        return std::unexpected(DescriptorError::MissingDefault);
    }
    if (!descriptor.defaultValue.is(descriptor.type)) {
        return std::unexpected(DescriptorError::DefaultTypeMismatch);
    }
    if (descriptor.flags.readOnly && descriptor.flags.automatable) {
        return std::unexpected(DescriptorError::ReadOnlyAndAutomatable);
    }
    if (descriptor.smoothingMs < 0.0f) {
        return std::unexpected(DescriptorError::NegativeSmoothing);
    }
    if (descriptor.smoothingMs > 0.0f && !hasRange(descriptor.type)) {
        return std::unexpected(DescriptorError::SmoothingOnUnsmoothableType);
    }
    if (const std::expected<void, DescriptorError> choices = validateChoices(descriptor); !choices) {
        return choices;
    }
    return validateRange(descriptor);
}

std::expected<void, ParamListError> validateParams(std::span<const ParamDescriptor> descriptors)
{
    std::unordered_set<std::string_view> seen;
    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        if (const std::expected<void, DescriptorError> one = validateParam(descriptors[index]); !one) {
            return std::unexpected(ParamListError{index, one.error()});
        }
        if (!seen.insert(descriptors[index].id).second) {
            return std::unexpected(ParamListError{index, DescriptorError::DuplicateId});
        }
    }
    return {};
}

bool isValueAllowed(const ParamDescriptor &descriptor, const ParamValue &value)
{
    if (!value.is(descriptor.type)) {
        return false;
    }
    if (descriptor.type == ParamType::Enum) {
        const std::string_view chosen = *value.asEnum();
        return std::ranges::find(descriptor.choices, chosen) != descriptor.choices.end();
    }
    if (!hasRange(descriptor.type) || descriptor.minimum.isEmpty()) {
        return true;
    }
    const double number = *value.asNumber();
    return number >= *descriptor.minimum.asNumber() && number <= *descriptor.maximum.asNumber();
}

ParamValue clampToRange(const ParamDescriptor &descriptor, const ParamValue &value)
{
    if (!value.is(descriptor.type) || !hasRange(descriptor.type) || descriptor.minimum.isEmpty()) {
        return value;
    }
    const double number = std::clamp(*value.asNumber(), *descriptor.minimum.asNumber(), *descriptor.maximum.asNumber());
    if (descriptor.type == ParamType::Int) {
        return ParamValue::ofInt(static_cast<std::int64_t>(std::llround(number)));
    }
    return ParamValue::ofFloat(static_cast<float>(number));
}

} // namespace rb::dsp

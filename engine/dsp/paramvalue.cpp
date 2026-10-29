// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/paramvalue.h"

#include <utility>

namespace rb::dsp
{

std::string_view paramTypeName(ParamType type) noexcept
{
    switch (type) {
    case ParamType::Float:
        return "float";
    case ParamType::Int:
        return "int";
    case ParamType::Bool:
        return "bool";
    case ParamType::Enum:
        return "enum";
    case ParamType::String:
        return "string";
    case ParamType::DeviceRef:
        return "deviceRef";
    case ParamType::StringList:
        return "stringList";
    }
    return "unknown";
}

ParamValue ParamValue::ofFloat(float value) noexcept
{
    ParamValue result;
    result.m_type = ParamType::Float;
    result.m_number = static_cast<double>(value);
    return result;
}

ParamValue ParamValue::ofInt(std::int64_t value) noexcept
{
    ParamValue result;
    result.m_type = ParamType::Int;
    result.m_number = static_cast<double>(value);
    return result;
}

ParamValue ParamValue::ofBool(bool value) noexcept
{
    ParamValue result;
    result.m_type = ParamType::Bool;
    result.m_boolean = value;
    return result;
}

ParamValue ParamValue::ofEnum(std::string choiceId)
{
    ParamValue result;
    result.m_type = ParamType::Enum;
    result.m_text = std::move(choiceId);
    return result;
}

ParamValue ParamValue::ofString(std::string text)
{
    ParamValue result;
    result.m_type = ParamType::String;
    result.m_text = std::move(text);
    return result;
}

ParamValue ParamValue::ofDeviceRef(std::string deviceName)
{
    ParamValue result;
    result.m_type = ParamType::DeviceRef;
    result.m_text = std::move(deviceName);
    return result;
}

ParamValue ParamValue::ofStringList(std::vector<std::string> values)
{
    ParamValue result;
    result.m_type = ParamType::StringList;
    result.m_list = std::move(values);
    return result;
}

std::optional<float> ParamValue::asFloat() const noexcept
{
    if (!is(ParamType::Float)) {
        return std::nullopt;
    }
    return static_cast<float>(m_number);
}

std::optional<std::int64_t> ParamValue::asInt() const noexcept
{
    if (!is(ParamType::Int)) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(m_number);
}

std::optional<bool> ParamValue::asBool() const noexcept
{
    if (!is(ParamType::Bool)) {
        return std::nullopt;
    }
    return m_boolean;
}

std::optional<std::string_view> ParamValue::asEnum() const & noexcept
{
    if (!is(ParamType::Enum)) {
        return std::nullopt;
    }
    return std::string_view{m_text};
}

std::optional<std::string_view> ParamValue::asString() const & noexcept
{
    if (!is(ParamType::String)) {
        return std::nullopt;
    }
    return std::string_view{m_text};
}

std::optional<std::string_view> ParamValue::asDeviceRef() const & noexcept
{
    if (!is(ParamType::DeviceRef)) {
        return std::nullopt;
    }
    return std::string_view{m_text};
}

std::optional<std::vector<std::string>> ParamValue::asStringList() const noexcept
{
    if (!is(ParamType::StringList)) {
        return std::nullopt;
    }
    return m_list;
}

std::optional<std::string_view> ParamValue::asText() const & noexcept
{
    if (!m_type.has_value()) {
        return std::nullopt;
    }
    switch (*m_type) {
    case ParamType::Enum:
    case ParamType::String:
    case ParamType::DeviceRef:
        return std::string_view{m_text};
    case ParamType::Float:
    case ParamType::Int:
    case ParamType::Bool:
    case ParamType::StringList:
        break;
    }
    return std::nullopt;
}

std::string ParamValue::text() const
{
    return std::string{asText().value_or("")};
}

std::optional<double> ParamValue::asNumber() const noexcept
{
    if (!m_type.has_value()) {
        return std::nullopt;
    }
    switch (*m_type) {
    case ParamType::Float:
    case ParamType::Int:
        return m_number;
    case ParamType::Bool:
        return m_boolean ? 1.0 : 0.0;
    case ParamType::Enum:
    case ParamType::String:
    case ParamType::DeviceRef:
    case ParamType::StringList:
        break;
    }
    return std::nullopt;
}

} // namespace rb::dsp

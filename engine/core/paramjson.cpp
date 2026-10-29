// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/paramjson.h"

#include <QJsonArray>

#include <string>
#include <string_view>
#include <vector>

namespace rb::core
{

[[nodiscard]] std::optional<dsp::ParamValue> valueFromJson(const dsp::ParamDescriptor &descriptor,
                                                           const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull()) {
        return std::nullopt;
    }
    switch (descriptor.type) {
    case dsp::ParamType::Float:
        return dsp::ParamValue::ofFloat(static_cast<float>(value.toDouble()));
    case dsp::ParamType::Int:
        return dsp::ParamValue::ofInt(value.toInteger());
    case dsp::ParamType::Bool:
        return dsp::ParamValue::ofBool(value.toBool());
    case dsp::ParamType::Enum:
        return dsp::ParamValue::ofEnum(value.toString().toStdString());
    case dsp::ParamType::String:
        return dsp::ParamValue::ofString(value.toString().toStdString());
    case dsp::ParamType::DeviceRef:
        return dsp::ParamValue::ofDeviceRef(value.toString().toStdString());
    case dsp::ParamType::StringList: {
        // A list is an array in the file, because a name with a comma in it
        // is a name. Anything that is not an array is not a list, and a
        // block keeps its default rather than being given nonsense.
        if (!value.isArray()) {
            return std::nullopt;
        }
        std::vector<std::string> values;
        const QJsonArray array = value.toArray();
        values.reserve(static_cast<std::size_t>(array.size()));
        for (const QJsonValue &entry : array) {
            values.push_back(entry.toString().toStdString());
        }
        return dsp::ParamValue::ofStringList(std::move(values));
    }
    }
    return std::nullopt;
}

[[nodiscard]] QJsonValue valueToJson(const dsp::ParamValue &value)
{
    if (const std::optional<double> number = value.asNumber(); number) {
        return value.type() == dsp::ParamType::Bool ? QJsonValue{*number != 0.0} : QJsonValue{*number};
    }
    if (const std::optional<std::vector<std::string>> values = value.asStringList(); values) {
        QJsonArray array;
        for (const std::string &entry : *values) {
            array.append(QString::fromStdString(entry));
        }
        return array;
    }
    const std::optional<std::string_view> text = value.asText();
    return text ? QJsonValue{QString::fromUtf8(text->data(), static_cast<qsizetype>(text->size()))} : QJsonValue{};
}

} // namespace rb::core

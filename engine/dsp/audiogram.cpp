// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/audiogram.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace rb::dsp
{

namespace
{

[[nodiscard]] ParamDescriptor levelDescriptor(std::string id)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(kAudiogramMinDb);
    descriptor.maximum = ParamValue::ofFloat(kAudiogramMaxDb);
    descriptor.defaultValue = ParamValue::ofFloat(0.0f);
    descriptor.unit = ParamUnit::Decibels;
    descriptor.flags.hidden = true;
    return descriptor;
}

/// \a value at \a hz from the points \a has picks out, on a straight line in
/// log frequency between the two either side, carried on past the ends.
template <typename Has, typename Value>
[[nodiscard]] std::optional<float> interpolate(const Audiogram &audiogram, double hz, Has has, Value value)
{
    std::optional<std::size_t> below;
    std::optional<std::size_t> above;
    for (std::size_t index = 0; index < kAudiogramPoints; ++index) {
        if (!has(audiogram.points[index])) {
            continue;
        }
        const auto frequency = static_cast<double>(kAudiogramFrequenciesHz[index]);
        if (frequency <= hz) {
            below = index;
        }
        if (frequency >= hz && !above) {
            above = index;
        }
    }
    if (!below && !above) {
        return std::nullopt;
    }
    if (!below) {
        return value(audiogram.points[*above]);
    }
    if (!above || *below == *above) {
        return value(audiogram.points[*below]);
    }
    const double low = std::log2(static_cast<double>(kAudiogramFrequenciesHz[*below]));
    const double high = std::log2(static_cast<double>(kAudiogramFrequenciesHz[*above]));
    const double along = (std::log2(hz) - low) / (high - low);
    const double from = value(audiogram.points[*below]);
    const double to = value(audiogram.points[*above]);
    return static_cast<float>(from + ((to - from) * along));
}

[[nodiscard]] bool measured(const AudiogramPoint &point) noexcept
{
    return point.mark != AudiogramMark::None;
}

} // namespace

bool Audiogram::isEmpty() const noexcept
{
    return std::ranges::none_of(points, measured);
}

std::string_view audiogramUnitName(AudiogramUnit unit) noexcept
{
    switch (unit) {
    case AudiogramUnit::DbHl:
        return "dbHl";
    case AudiogramUnit::DbFs:
        return "dbFs";
    }
    return "dbHl";
}

std::optional<AudiogramUnit> audiogramUnitFromName(std::string_view name) noexcept
{
    for (const AudiogramUnit unit : {AudiogramUnit::DbHl, AudiogramUnit::DbFs}) {
        if (audiogramUnitName(unit) == name) {
            return unit;
        }
    }
    return std::nullopt;
}

std::string_view audiogramMarkName(AudiogramMark mark) noexcept
{
    switch (mark) {
    case AudiogramMark::None:
        return "none";
    case AudiogramMark::Heard:
        return "heard";
    case AudiogramMark::NoResponse:
        return "noResponse";
    }
    return "none";
}

std::optional<AudiogramMark> audiogramMarkFromName(std::string_view name) noexcept
{
    for (const AudiogramMark mark : {AudiogramMark::None, AudiogramMark::Heard, AudiogramMark::NoResponse}) {
        if (audiogramMarkName(mark) == name) {
            return mark;
        }
    }
    return std::nullopt;
}

std::string audiogramParamId(std::size_t point, AudiogramField field)
{
    std::string id = "audiogram" + std::to_string(kAudiogramFrequenciesHz.at(point));
    switch (field) {
    case AudiogramField::Mark:
        break;
    case AudiogramField::LevelDb:
        id += "Db";
        break;
    case AudiogramField::TooLoudGiven:
        id += "TooLoud";
        break;
    case AudiogramField::TooLoudDb:
        id += "TooLoudDb";
        break;
    }
    return id;
}

std::vector<ParamDescriptor> audiogramParams()
{
    std::vector<ParamDescriptor> descriptors;
    descriptors.reserve(1 + (kAudiogramPoints * 4));

    ParamDescriptor unit;
    unit.id = std::string{kAudiogramUnitParamId};
    unit.type = ParamType::Enum;
    unit.choices = {std::string{audiogramUnitName(AudiogramUnit::DbHl)},
                    std::string{audiogramUnitName(AudiogramUnit::DbFs)}};
    unit.defaultValue = ParamValue::ofEnum(std::string{audiogramUnitName(AudiogramUnit::DbHl)});
    unit.flags.hidden = true;
    descriptors.push_back(std::move(unit));

    for (std::size_t point = 0; point < kAudiogramPoints; ++point) {
        ParamDescriptor mark;
        mark.id = audiogramParamId(point, AudiogramField::Mark);
        mark.type = ParamType::Enum;
        mark.choices = {std::string{audiogramMarkName(AudiogramMark::None)},
                        std::string{audiogramMarkName(AudiogramMark::Heard)},
                        std::string{audiogramMarkName(AudiogramMark::NoResponse)}};
        mark.defaultValue = ParamValue::ofEnum(std::string{audiogramMarkName(AudiogramMark::None)});
        mark.flags.hidden = true;
        descriptors.push_back(std::move(mark));

        descriptors.push_back(levelDescriptor(audiogramParamId(point, AudiogramField::LevelDb)));

        ParamDescriptor tooLoudGiven;
        tooLoudGiven.id = audiogramParamId(point, AudiogramField::TooLoudGiven);
        tooLoudGiven.type = ParamType::Bool;
        tooLoudGiven.defaultValue = ParamValue::ofBool(false);
        tooLoudGiven.flags.hidden = true;
        descriptors.push_back(std::move(tooLoudGiven));

        ParamDescriptor tooLoud = levelDescriptor(audiogramParamId(point, AudiogramField::TooLoudDb));
        tooLoud.defaultValue = ParamValue::ofFloat(100.0f);
        descriptors.push_back(std::move(tooLoud));
    }
    return descriptors;
}

std::optional<float> audiogramLevelAt(const Audiogram &audiogram, double hz)
{
    return interpolate(audiogram, hz, measured, [](const AudiogramPoint &point) { return point.levelDb; });
}

std::optional<std::size_t> nearestMeasuredPoint(const Audiogram &audiogram, double hz)
{
    std::optional<std::size_t> nearest;
    double nearestDistance = 0.0;
    for (std::size_t index = 0; index < kAudiogramPoints; ++index) {
        if (!measured(audiogram.points[index])) {
            continue;
        }
        const double distance = std::abs(std::log2(static_cast<double>(kAudiogramFrequenciesHz[index]) / hz));
        if (!nearest || distance < nearestDistance) {
            nearest = index;
            nearestDistance = distance;
        }
    }
    return nearest;
}

std::optional<float> audiogramTooLoudAt(const Audiogram &audiogram, double hz)
{
    return interpolate(
        audiogram,
        hz,
        [](const AudiogramPoint &point) { return point.tooLoudDb.has_value(); },
        [](const AudiogramPoint &point) { return *point.tooLoudDb; });
}

} // namespace rb::dsp

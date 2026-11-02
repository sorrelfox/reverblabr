// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "model/description.h"

#include "model/resampler.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace rb::model
{

namespace
{

/// Longest line a host may send: a file name and a little.
constexpr std::size_t kLongestText = 1 << 14;

template <typename Number>
[[nodiscard]] Number number(std::string_view text)
{
    Number value{};
    const auto [end, fault] = std::from_chars(text.data(), text.data() + text.size(), value);
    return fault == std::errc{} && end == text.data() + text.size() ? value : Number{};
}

} // namespace

std::string describeAsText(const Description &description)
{
    std::string text;
    const auto line = [&text](std::string_view key, const auto &value) {
        text += key;
        text += '=';
        if constexpr (std::is_arithmetic_v<std::decay_t<decltype(value)>>) {
            text += std::to_string(value);
        } else {
            text += value;
        }
        text += '\n';
    };
    line("refusal", static_cast<std::uint32_t>(description.refusal));
    line("family", static_cast<std::uint32_t>(description.family));
    line("modelFile", description.modelFile);
    line("licenceFile", description.licenceFile);
    line("sampleRate", description.sampleRate);
    line("channels", description.channels);
    line("frameSize", description.frameSize);
    line("hopSize", description.hopSize);
    line("latencySamples", description.latencySamples);
    return text;
}

Description parseDescriptionText(std::string_view text)
{
    Description description;
    description.refusal = Refusal::NoHost;
    if (text.size() > kLongestText) {
        return description;
    }
    bool saidRefusal = false;
    while (!text.empty()) {
        const std::size_t end = std::min(text.find('\n'), text.size());
        const std::string_view line = text.substr(0, end);
        text.remove_prefix(std::min(end + 1, text.size()));
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }
        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);
        if (key == "refusal") {
            description.refusal = static_cast<Refusal>(number<std::uint32_t>(value));
            saidRefusal = true;
        } else if (key == "family") {
            description.family = static_cast<Family>(number<std::uint32_t>(value));
        } else if (key == "modelFile") {
            description.modelFile = value;
        } else if (key == "licenceFile") {
            description.licenceFile = value;
        } else if (key == "sampleRate") {
            description.sampleRate = number<std::uint32_t>(value);
        } else if (key == "channels") {
            description.channels = number<std::uint32_t>(value);
        } else if (key == "frameSize") {
            description.frameSize = number<std::uint32_t>(value);
        } else if (key == "hopSize") {
            description.hopSize = number<std::uint32_t>(value);
        } else if (key == "latencySamples") {
            description.latencySamples = number<std::uint32_t>(value);
        }
    }
    if (!saidRefusal) {
        description.refusal = Refusal::NoHost;
    }
    return description;
}

std::optional<Timing> timingFor(const Description &description, std::uint32_t sessionRate, std::uint32_t spareFrames)
{
    if (!description.usable() || sessionRate == 0 || description.sampleRate == 0 || description.hopSize == 0) {
        return std::nullopt;
    }
    const std::uint32_t modelRate = description.sampleRate;
    if (!Resampler::supported(sessionRate, modelRate) || !Resampler::supported(modelRate, sessionRate)) {
        return std::nullopt;
    }
    const double session = sessionRate;
    const double model = modelRate;
    const double seconds = Resampler::delaySeconds(sessionRate, modelRate)
        + Resampler::delaySeconds(modelRate, sessionRate) + description.latencySamples / model;
    Timing timing;
    timing.hopFrames = static_cast<std::uint32_t>(std::ceil(description.hopSize * session / model));
    timing.pathDelay = static_cast<std::uint32_t>(std::lround(seconds * session));
    timing.latency = timing.hopFrames + timing.pathDelay + spareFrames;
    return timing;
}

} // namespace rb::model

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
    line("contentFile", description.voice.contentFile);
    line("pitchFile", description.voice.pitchFile);
    line("sampleRate", description.sampleRate);
    line("inputRate", description.inputRate);
    line("channels", description.channels);
    line("frameSize", description.frameSize);
    line("hopSize", description.hopSize);
    line("outputHop", description.outputHop);
    line("latencySamples", description.latencySamples);
    line("runMs", description.runMs);
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
        } else if (key == "contentFile") {
            description.voice.contentFile = value;
        } else if (key == "pitchFile") {
            description.voice.pitchFile = value;
        } else if (key == "sampleRate") {
            description.sampleRate = number<std::uint32_t>(value);
        } else if (key == "inputRate") {
            description.inputRate = number<std::uint32_t>(value);
        } else if (key == "channels") {
            description.channels = number<std::uint32_t>(value);
        } else if (key == "frameSize") {
            description.frameSize = number<std::uint32_t>(value);
        } else if (key == "hopSize") {
            description.hopSize = number<std::uint32_t>(value);
        } else if (key == "outputHop") {
            description.outputHop = number<std::uint32_t>(value);
        } else if (key == "latencySamples") {
            description.latencySamples = number<std::uint32_t>(value);
        } else if (key == "runMs") {
            description.runMs = number<std::uint32_t>(value);
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
    const std::uint32_t rateOut = description.sampleRate;
    const std::uint32_t rateIn = description.rateIn();
    if (!Resampler::supported(sessionRate, rateIn) || !Resampler::supported(rateOut, sessionRate)) {
        return std::nullopt;
    }
    const double session = sessionRate;
    const double seconds = Resampler::delaySeconds(sessionRate, rateIn) + Resampler::delaySeconds(rateOut, sessionRate)
        + description.latencySamples / static_cast<double>(rateOut);
    Timing timing;
    timing.hopFrames = static_cast<std::uint32_t>(std::ceil(description.hopSize * session / rateIn));
    timing.pathDelay = static_cast<std::uint32_t>(std::lround(seconds * session));
    timing.latency = timing.hopFrames + timing.pathDelay + spareFrames
        + static_cast<std::uint32_t>(std::lround(description.runMs * session / 1000.0));
    return timing;
}

} // namespace rb::model

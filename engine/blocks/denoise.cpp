// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/denoise.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

/// How much of the last frames' power a band's estimate keeps. A single
/// frame's power of a steady noise scatters around its mean by as much as
/// the mean itself, and a band judged on that alone flickers between kept
/// and removed: the "musical noise" a plain subtraction is known for. This
/// averages it over the last few frames.
constexpr float kPowerSmoothing = 0.7f;
/// And how much of the last frame's gain a band keeps, which rounds off
/// what is left of the flicker.
constexpr float kGainSmoothing = 0.2f;
/// sqrt-Hann in and out, four frames over every sample: the windows'
/// products add to 2, so the sum is halved.
constexpr float kOverlapScale = 0.5f;
/// The header of a profile's text.
constexpr std::string_view kProfileVersion = "v1";

[[nodiscard]] bool parseNumber(std::string_view text, double &value)
{
    const char *const end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

} // namespace

std::vector<ParamDescriptor> Denoise::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor reduction;
    reduction.id = "reduction";
    reduction.type = ParamType::Float;
    reduction.minimum = ParamValue::ofFloat(0.0f);
    reduction.maximum = ParamValue::ofFloat(kHighestReductionDb);
    reduction.defaultValue = ParamValue::ofFloat(kDefaultReductionDb);
    reduction.unit = ParamUnit::Decibels;
    descriptors.push_back(std::move(reduction));

    ParamDescriptor threshold;
    threshold.id = "threshold";
    threshold.type = ParamType::Float;
    threshold.minimum = ParamValue::ofFloat(0.0f);
    threshold.maximum = ParamValue::ofFloat(kHighestThresholdDb);
    threshold.defaultValue = ParamValue::ofFloat(kDefaultThresholdDb);
    threshold.unit = ParamUnit::Decibels;
    descriptors.push_back(std::move(threshold));

    // **Counts, not switches**: pressing Learn twice has to start it twice,
    // and a switch left on after the first would not change the second time.
    ParamDescriptor learn;
    learn.id = "learn";
    learn.type = ParamType::Int;
    learn.minimum = ParamValue::ofInt(0);
    learn.maximum = ParamValue::ofInt(kMaxIntParamMagnitude);
    learn.defaultValue = ParamValue::ofInt(0);
    learn.flags.hidden = true;
    descriptors.push_back(std::move(learn));

    ParamDescriptor forget;
    forget.id = "forget";
    forget.type = ParamType::Int;
    forget.minimum = ParamValue::ofInt(0);
    forget.maximum = ParamValue::ofInt(kMaxIntParamMagnitude);
    forget.defaultValue = ParamValue::ofInt(0);
    forget.flags.hidden = true;
    descriptors.push_back(std::move(forget));

    // **Not automatable**: a profile arriving from outside (a preset, an
    // undo) builds the block afresh with it rather than being sent to the
    // audio thread, which has nowhere to put a thousand numbers mid-cycle.
    ParamDescriptor profile;
    profile.id = "profile";
    profile.type = ParamType::String;
    profile.defaultValue = ParamValue::ofString("");
    profile.flags.hidden = true;
    profile.flags.automatable = false;
    descriptors.push_back(std::move(profile));
    return descriptors;
}

Denoise::Denoise()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    m_info.latencySamples = static_cast<std::uint32_t>(kFrameSize);
}

std::unique_ptr<Block> Denoise::make()
{
    return std::make_unique<Denoise>();
}

std::string Denoise::encodeProfile(const NoiseProfile &profile)
{
    if (!profile.valid) {
        return {};
    }
    std::string text{kProfileVersion};
    text += ";rate=" + std::to_string(static_cast<long long>(std::lround(profile.sampleRate)));
    text += ";size=" + std::to_string(kFrameSize);
    char seconds[32];
    std::snprintf(seconds, sizeof seconds, "%.1f", static_cast<double>(profile.seconds));
    text += ";seconds=";
    text += seconds;
    text += ";db=";
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        char number[32];
        // To a tenth, and never "-0.0": the text read back and written
        // again has to be the same text.
        double db = std::round(static_cast<double>(gainToDb(std::sqrt(profile.power[bin]))) * 10.0) / 10.0;
        if (db == 0.0) {
            db = 0.0;
        }
        std::snprintf(number, sizeof number, "%.1f", db);
        if (bin > 0) {
            text += ',';
        }
        text += number;
    }
    return text;
}

std::optional<Denoise::NoiseProfile> Denoise::decodeProfile(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    NoiseProfile profile;
    std::size_t fields = 0;
    while (!text.empty()) {
        const std::size_t end = text.find(';');
        const std::string_view field = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (fields++ == 0) {
            if (field != kProfileVersion) {
                return std::nullopt;
            }
            continue;
        }
        const std::size_t equals = field.find('=');
        if (equals == std::string_view::npos) {
            return std::nullopt;
        }
        const std::string_view key = field.substr(0, equals);
        const std::string_view value = field.substr(equals + 1);
        double number = 0.0;
        if (key == "rate") {
            if (!parseNumber(value, number) || !(number > 0.0)) {
                return std::nullopt;
            }
            profile.sampleRate = number;
        } else if (key == "size") {
            if (!parseNumber(value, number) || number != static_cast<double>(kFrameSize)) {
                return std::nullopt;
            }
        } else if (key == "seconds") {
            if (!parseNumber(value, number) || number < 0.0) {
                return std::nullopt;
            }
            profile.seconds = static_cast<float>(number);
        } else if (key == "db") {
            std::string_view list = value;
            std::size_t bin = 0;
            while (!list.empty()) {
                const std::size_t comma = list.find(',');
                if (bin >= kBins || !parseNumber(list.substr(0, comma), number)) {
                    return std::nullopt;
                }
                const float amplitude = dbToGain(static_cast<float>(number));
                profile.power[bin++] = amplitude * amplitude;
                list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
            }
            if (bin != kBins) {
                return std::nullopt;
            }
            profile.valid = true;
        }
    }
    if (!profile.valid || !(profile.sampleRate > 0.0)) {
        return std::nullopt;
    }
    return profile;
}

void Denoise::adoptStoredProfile()
{
    m_haveProfile = m_stored.valid;
    m_profileSeconds = m_stored.seconds;
    if (!m_stored.valid) {
        std::ranges::fill(m_noise, 0.0f);
        return;
    }
    // **At the running rate**: a profile learned at 44.1 kHz is used at 48
    // by reading each band at the frequency it now stands for.
    const double ratio = m_sampleRate / m_stored.sampleRate;
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const double at = static_cast<double>(bin) * ratio;
        const auto below = std::min(static_cast<std::size_t>(at), kBins - 1);
        const std::size_t above = std::min(below + 1, kBins - 1);
        const double fraction = std::min(1.0, at - static_cast<double>(below));
        m_noise[bin] = static_cast<float>(static_cast<double>(m_stored.power[below]) * (1.0 - fraction)
                                          + static_cast<double>(m_stored.power[above]) * fraction);
    }
}

std::expected<void, BlockError> Denoise::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_meter = spec.meter;
    m_fft.prepare(kFrameSize);
    m_window.resize(kFrameSize);
    for (std::size_t n = 0; n < kFrameSize; ++n) {
        // Periodic Hann's square root, so the window in and the window out
        // together are one Hann.
        const double hann = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / kFrameSize);
        m_window[n] = static_cast<float>(std::sqrt(hann));
    }
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        m_input[channel].assign(kFrameSize, 0.0f);
        m_output[channel].assign(kFrameSize, 0.0f);
        m_ready[channel].assign(kHop, 0.0f);
        m_spectrum[channel].assign(kBins, {0.0f, 0.0f});
    }
    m_frame.assign(kFrameSize, 0.0f);
    m_noise.assign(kBins, 0.0f);
    m_gains.assign(kBins, 1.0f);
    m_power.assign(kBins, 0.0f);
    m_learnSum.assign(kBins, 0.0);
    m_learnTarget = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::lround(kLearnSeconds * spec.sampleRate / static_cast<double>(kHop))));
    m_position = kFrameSize - kHop;
    m_seenLearn = m_store.number(Learn);
    m_seenForget = m_store.number(Forget);
    m_learning = false;
    adoptStoredProfile();
    m_prepared = true;
    return {};
}

void Denoise::publishProfile(bool valid) noexcept RB_NONBLOCKING
{
    NoiseProfile &slot = m_published.writeSlot();
    slot.valid = valid;
    slot.sampleRate = m_sampleRate;
    slot.seconds = m_profileSeconds;
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        slot.power[bin] = m_noise[bin];
    }
    m_published.publish();
    m_publishedCount.fetch_add(1, std::memory_order_release);
}

void Denoise::finishLearning() noexcept RB_NONBLOCKING
{
    const double frames = static_cast<double>(m_learnFrames * kChannels);
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        m_noise[bin] = static_cast<float>(m_learnSum[bin] / frames);
    }
    m_profileSeconds = static_cast<float>(static_cast<double>(m_learnFrames * kHop) / m_sampleRate);
    m_haveProfile = true;
    m_learning = false;
    publishProfile(true);
}

void Denoise::processFrame() noexcept RB_NONBLOCKING
{
    // Both channels into the frequency domain.
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        for (std::size_t n = 0; n < kFrameSize; ++n) {
            m_frame[n] = m_input[channel][n] * m_window[n];
        }
        m_fft.forward(m_frame, m_spectrum[channel]);
    }

    if (m_learning) {
        for (std::size_t bin = 0; bin < kBins; ++bin) {
            for (std::size_t channel = 0; channel < kChannels; ++channel) {
                m_learnSum[bin] += static_cast<double>(std::norm(m_spectrum[channel][bin]));
            }
        }
        if (++m_learnFrames >= m_learnTarget) {
            finishLearning();
        }
    }

    // One gain per band for both sides, from their average power.
    const float floor = dbToGain(-m_store.number(Reduction));
    const float strength = dbToGain(m_store.number(Threshold) * 2.0f); // as a power ratio
    const bool removing = m_haveProfile && !m_learning;
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const float power = 0.5f * (std::norm(m_spectrum[0][bin]) + std::norm(m_spectrum[1][bin]));
        m_power[bin] = kPowerSmoothing * m_power[bin] + (1.0f - kPowerSmoothing) * power;
        float target = 1.0f;
        if (removing) {
            target = m_power[bin] > 0.0f ? std::max(floor, 1.0f - strength * m_noise[bin] / m_power[bin]) : floor;
        }
        m_gains[bin] = kGainSmoothing * m_gains[bin] + (1.0f - kGainSmoothing) * target;
    }

    // Back, windowed again, into each channel's overlap-add.
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        for (std::size_t bin = 0; bin < kBins; ++bin) {
            m_spectrum[channel][bin] *= m_gains[bin];
        }
        m_fft.inverse(m_spectrum[channel], m_frame);
        std::vector<float> &output = m_output[channel];
        for (std::size_t n = 0; n < kFrameSize; ++n) {
            output[n] += m_frame[n] * m_window[n] * kOverlapScale;
        }
        // The first hop has had every frame it will get: it is ready.
        std::copy(output.begin(), output.begin() + kHop, m_ready[channel].begin());
        std::copy(output.begin() + kHop, output.end(), output.begin());
        std::fill(output.end() - kHop, output.end(), 0.0f);
        std::vector<float> &input = m_input[channel];
        std::copy(input.begin() + kHop, input.end(), input.begin());
    }
}

void Denoise::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    // A new count is a press of the button.
    if (m_store.number(Forget) != m_seenForget) {
        m_seenForget = m_store.number(Forget);
        m_learning = false;
        m_haveProfile = false;
        m_profileSeconds = 0.0f;
        std::ranges::fill(m_noise, 0.0f);
        publishProfile(false);
    }
    if (m_store.number(Learn) != m_seenLearn) {
        m_seenLearn = m_store.number(Learn);
        m_learning = true;
        m_learnFrames = 0;
        std::ranges::fill(m_learnSum, 0.0);
    }

    std::array<std::span<float>, kChannels> in;
    std::array<std::span<float>, kChannels> out;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        in[channel] = context.input.channel(static_cast<std::uint32_t>(channel));
        out[channel] = context.output.channel(static_cast<std::uint32_t>(channel));
    }
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const std::size_t slot = m_position - (kFrameSize - kHop);
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            // Read before written: the graph may hand one buffer for both.
            const float sample = frame < in[channel].size() ? in[channel][frame] : 0.0f;
            if (frame < out[channel].size()) {
                out[channel][frame] = m_ready[channel][slot];
            }
            m_input[channel][m_position] = sample;
        }
        if (++m_position == kFrameSize) {
            processFrame();
            m_position = kFrameSize - kHop;
        }
    }

    if (m_meter != nullptr) {
        const float state = m_learning ? kLearning : (m_haveProfile ? kActive : kIdle);
        m_meter->publish(kStateValue, state);
        m_meter->publish(kProgressValue,
                         m_learning ? static_cast<float>(m_learnFrames) / static_cast<float>(m_learnTarget) : 0.0f);
        m_meter->publish(kLearnedSecondsValue, m_haveProfile ? m_profileSeconds : 0.0f);
    }
}

void Denoise::reset() noexcept RB_NONBLOCKING
{
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        std::ranges::fill(m_input[channel], 0.0f);
        std::ranges::fill(m_output[channel], 0.0f);
        std::ranges::fill(m_ready[channel], 0.0f);
    }
    std::ranges::fill(m_gains, 1.0f);
    std::ranges::fill(m_power, 0.0f);
    m_position = kFrameSize - kHop;
}

void Denoise::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Every number reaches the audio thread this way; a new Learn or Forget
    // count is acted on at the top of the next cycle.
    m_store.applyNumber(index, value);
}

std::optional<std::string> Denoise::learnedProfileText() const
{
    if (m_publishedCount.load(std::memory_order_acquire) == 0) {
        return std::nullopt;
    }
    return encodeProfile(m_published.read());
}

std::expected<void, BlockError> Denoise::setParam(std::string_view id, const ParamValue &value)
{
    const std::expected<void, BlockError> set = m_store.set(id, value);
    if (set && id == "profile") {
        m_stored = decodeProfile(m_store.value(id).text()).value_or(NoiseProfile{});
    }
    return set;
}

ParamValue Denoise::param(std::string_view id) const
{
    if (id == "profile") {
        if (const std::optional<std::string> learned = learnedProfileText()) {
            return ParamValue::ofString(*learned);
        }
    }
    return m_store.value(id);
}

ParamValueMap Denoise::saveState() const
{
    ParamValueMap state = m_store.save();
    // **What the audio thread learned, not what was loaded**, once it has
    // learned or forgotten anything: saving writes what is being heard.
    if (const std::optional<std::string> learned = learnedProfileText()) {
        state.insert_or_assign("profile", ParamValue::ofString(*learned));
    }
    return state;
}

std::expected<void, BlockError> Denoise::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded) {
        m_stored = decodeProfile(m_store.value("profile").text()).value_or(NoiseProfile{});
        if (m_prepared) {
            m_seenLearn = m_store.number(Learn);
            m_seenForget = m_store.number(Forget);
            adoptStoredProfile();
        }
    }
    return loaded;
}

} // namespace rb::blocks

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

/// sqrt-Hann in and out, four frames over every sample: the windows'
/// products add to 2, so the sum is halved.
constexpr float kOverlapScale = 0.5f;
/// The header of a profile's text. "v1", an average per band, is not read.
constexpr std::string_view kProfileVersion = "v2";
/// Where each octave but the last ends, in Hz.
constexpr std::array<double, Denoise::kOctaves - 1> kOctaveEdges{150.0, 300.0, 600.0, 1200.0, 2400.0, 4800.0, 9600.0};
/// A band's power under this is no power at all: its level is kLowestDb.
constexpr float kNoPower = 1.0e-18f;

[[nodiscard]] bool parseNumber(std::string_view text, double &value)
{
    const char *const end = text.data() + text.size();
    const std::from_chars_result result = std::from_chars(text.data(), end, value);
    return result.ec == std::errc{} && result.ptr == end && std::isfinite(value);
}

/// Exactly N numbers, between commas, into \a values.
template <std::size_t N>
[[nodiscard]] bool parseList(std::string_view text, std::array<float, N> &values)
{
    std::size_t index = 0;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        double number = 0.0;
        if (index >= N || !parseNumber(text.substr(0, comma), number)) {
            return false;
        }
        values[index++] = static_cast<float>(number);
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
    }
    return index == N;
}

/// ";key=" and \a values to a tenth, between commas.
template <std::size_t N>
void appendList(std::string &text, std::string_view key, const std::array<float, N> &values)
{
    text += ';';
    text += key;
    text += '=';
    for (std::size_t index = 0; index < N; ++index) {
        char number[32];
        // Never "-0.0": the text read back and written again has to be the
        // same text.
        double value = std::round(static_cast<double>(values[index]) * 10.0) / 10.0;
        if (value == 0.0) {
            value = 0.0;
        }
        std::snprintf(number, sizeof number, "%.1f", value);
        if (index > 0) {
            text += ',';
        }
        text += number;
    }
}

/// Which 1 dB step of the counts \a level falls in.
[[nodiscard]] std::size_t stepOf(float level, std::size_t steps) noexcept RB_NONBLOCKING
{
    const float above = level - Denoise::kLowestDb;
    if (!(above > 0.0f)) {
        return 0;
    }
    return std::min(static_cast<std::size_t>(above), steps - 1);
}

/// \a a and \a b in order, without a branch.
inline void order(float &a, float &b) noexcept RB_NONBLOCKING
{
    const float low = std::min(a, b);
    const float high = std::max(a, b);
    a = low;
    b = high;
}

/// One more in a count, which stops at its highest.
void countOne(std::uint16_t &count) noexcept RB_NONBLOCKING
{
    if (count < 0xFFFF) {
        ++count;
    }
}

/// The level a share 1 − kQuantile of the counted levels of bands \a first
/// to \a last, taken together, are over: found from the top step any of
/// them reached, \a top, down, and placed within its step as if the levels
/// in it were spread evenly.
[[nodiscard]] float ceilingOf(std::span<const std::uint16_t> counts,
                              std::size_t steps,
                              std::size_t first,
                              std::size_t last,
                              std::size_t top,
                              std::size_t frames) noexcept RB_NONBLOCKING
{
    const double total = static_cast<double>(frames) * static_cast<double>(last - first + 1);
    const double over = (1.0 - Denoise::kQuantile) * total;
    double above = 0.0;
    for (std::size_t step = top + 1; step-- > 0;) {
        double here = 0.0;
        for (std::size_t band = first; band <= last; ++band) {
            here += static_cast<double>(counts[band * steps + step]);
        }
        if (here > 0.0 && above + here >= over) {
            const double inside = (over - above) / here;
            return Denoise::kLowestDb + static_cast<float>(static_cast<double>(step + 1) - inside);
        }
        above += here;
    }
    return Denoise::kLowestDb;
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
    // audio thread, which has nowhere to put two thousand numbers mid-cycle.
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

std::size_t Denoise::octaveOf(double frequency) noexcept RB_NONBLOCKING
{
    std::size_t octave = 0;
    while (octave < kOctaveEdges.size() && frequency >= kOctaveEdges[octave]) {
        ++octave;
    }
    return octave;
}

std::size_t Denoise::heldFrames(double sampleRate) noexcept
{
    // An odd count, so that the median is one frame's level: the nearest
    // to kHeldSeconds, which at 44.1 and 48 kHz is seven.
    const double frames = kHeldSeconds * sampleRate / static_cast<double>(kHop);
    const auto odd = static_cast<std::size_t>(2 * std::max(1L, std::lround((frames - 1.0) / 2.0)) + 1);
    return std::min(odd, kMostHeldFrames - 1);
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
    appendList(text, "now", profile.now);
    appendList(text, "held", profile.held);
    appendList(text, "nowlimits", profile.nowLimits);
    appendList(text, "heldlimits", profile.heldLimits);
    appendList(text, "rest", profile.rest);
    return text;
}

std::optional<Denoise::NoiseProfile> Denoise::decodeProfile(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    NoiseProfile profile;
    // Which of the five lists have been read.
    unsigned lists = 0;
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
        bool read = true;
        if (key == "rate") {
            read = parseNumber(value, number) && number > 0.0;
            profile.sampleRate = number;
        } else if (key == "size") {
            read = parseNumber(value, number) && number == static_cast<double>(kFrameSize);
        } else if (key == "seconds") {
            read = parseNumber(value, number) && number >= 0.0;
            profile.seconds = static_cast<float>(number);
        } else if (key == "now") {
            read = parseList(value, profile.now);
            lists |= 1U;
        } else if (key == "held") {
            read = parseList(value, profile.held);
            lists |= 2U;
        } else if (key == "nowlimits") {
            read = parseList(value, profile.nowLimits);
            lists |= 4U;
        } else if (key == "heldlimits") {
            read = parseList(value, profile.heldLimits);
            lists |= 8U;
        } else if (key == "rest") {
            read = parseList(value, profile.rest);
            lists |= 16U;
        }
        if (!read) {
            return std::nullopt;
        }
    }
    if (lists != 31U || !(profile.sampleRate > 0.0)) {
        return std::nullopt;
    }
    profile.valid = true;
    return profile;
}

void Denoise::adoptStoredProfile()
{
    m_haveProfile = m_stored.valid;
    m_profileSeconds = m_stored.seconds;
    m_followed.fill(0.0f);
    if (!m_stored.valid) {
        std::ranges::fill(m_ceilingNow, kLowestDb);
        std::ranges::fill(m_ceilingHeld, kLowestDb);
        m_nowLimit.fill(0.0f);
        m_heldLimit.fill(0.0f);
        m_rest.fill(0.0f);
        return;
    }
    // **At the running rate**: a profile learned at 44.1 kHz is used at 48
    // by reading each band at the frequency it now stands for. A band here
    // is wider by the ratio of the rates, and a broadband noise is louder in
    // it by as much.
    const double ratio = m_sampleRate / m_stored.sampleRate;
    const auto wider = static_cast<float>(10.0 * std::log10(ratio));
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const double at = static_cast<double>(bin) * ratio;
        const auto below = std::min(static_cast<std::size_t>(at), kBins - 1);
        const std::size_t above = std::min(below + 1, kBins - 1);
        const auto fraction = static_cast<float>(std::min(1.0, at - static_cast<double>(below)));
        m_ceilingNow[bin] = std::lerp(m_stored.now[below], m_stored.now[above], fraction) + wider;
        m_ceilingHeld[bin] = std::lerp(m_stored.held[below], m_stored.held[above], fraction) + wider;
    }
    // An octave's limits are sums over its bands: they scale with how many
    // bands the octave has here against there.
    std::array<std::size_t, kOctaves> there{};
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        ++there[octaveOf(static_cast<double>(bin) * m_stored.sampleRate / static_cast<double>(kFrameSize))];
    }
    for (std::size_t octave = 0; octave < kOctaves; ++octave) {
        const float scale =
            there[octave] > 0 ? static_cast<float>(m_octaveBands[octave]) / static_cast<float>(there[octave]) : 1.0f;
        m_nowLimit[octave] = m_stored.nowLimits[octave] * scale;
        m_heldLimit[octave] = m_stored.heldLimits[octave] * scale;
    }
    m_rest = m_stored.rest;
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
    double windowSum = 0.0;
    for (std::size_t n = 0; n < kFrameSize; ++n) {
        // Periodic Hann's square root, so the window in and the window out
        // together are one Hann.
        const double hann = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(n) / kFrameSize);
        m_window[n] = static_cast<float>(std::sqrt(hann));
        windowSum += static_cast<double>(m_window[n]);
    }
    // A full-scale sine on a band comes out at half the window's sum.
    m_levelScale = static_cast<float>(1.0 / (0.25 * windowSum * windowSum));
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        m_input[channel].assign(kFrameSize, 0.0f);
        m_output[channel].assign(kFrameSize, 0.0f);
        m_ready[channel].assign(kHop, 0.0f);
        m_spectrum[channel].assign(kBins, {0.0f, 0.0f});
    }
    m_frame.assign(kFrameSize, 0.0f);

    m_octave.assign(kBins, 0);
    m_octaveBands.fill(0);
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const std::size_t octave = octaveOf(static_cast<double>(bin) * m_sampleRate / static_cast<double>(kFrameSize));
        m_octave[bin] = static_cast<std::uint8_t>(octave);
        ++m_octaveBands[octave];
    }
    m_octaveStart[0] = 0;
    for (std::size_t octave = 0; octave < kOctaves; ++octave) {
        m_octaveStart[octave + 1] = m_octaveStart[octave] + m_octaveBands[octave];
    }
    m_heldFrames = heldFrames(m_sampleRate);
    m_ring.assign(m_heldFrames * kBins, kLowestDb);
    m_ringSlot = 0;
    m_level.assign(kBins, kLowestDb);
    m_held.assign(kBins, kLowestDb);
    m_excessNow.assign(kBins, 0.0f);
    m_excessHeld.assign(kBins, 0.0f);
    m_scratch.assign(kBins, 0.0f);
    m_ceilingNow.assign(kBins, kLowestDb);
    m_ceilingHeld.assign(kBins, kLowestDb);
    m_gainDb.assign(kBins, 0.0f);
    m_gain.assign(kBins, 1.0f);
    m_countNow.assign(kBins * kSteps, 0);
    m_countHeld.assign(kBins * kSteps, 0);
    m_topNow.assign(kBins, 0);
    m_topHeld.assign(kBins, 0);
    m_learnTarget = std::max<std::size_t>(
        2, static_cast<std::size_t>(std::lround(kLearnSeconds * spec.sampleRate / static_cast<double>(kHop))));
    m_followStep = static_cast<float>(static_cast<double>(kHop * kFollowEvery) / (kFollowSeconds * m_sampleRate));
    m_followTick = 0;
    m_position = kFrameSize - kHop;
    m_seenLearn = m_store.number(Learn);
    m_seenForget = m_store.number(Forget);
    m_learning = false;
    m_noiseAlone = false;
    adoptStoredProfile();
    // **A learned noise starts turned down**: what comes first is more
    // likely the noise than a sound, and a sound opens its bands at once.
    if (m_haveProfile) {
        closeAll();
    }
    m_prepared = true;
    return {};
}

void Denoise::publishProfile(bool valid) noexcept RB_NONBLOCKING
{
    NoiseProfile &slot = m_published.writeSlot();
    slot.valid = valid;
    slot.sampleRate = m_sampleRate;
    slot.seconds = m_profileSeconds;
    std::copy(m_ceilingNow.begin(), m_ceilingNow.end(), slot.now.begin());
    std::copy(m_ceilingHeld.begin(), m_ceilingHeld.end(), slot.held.begin());
    slot.nowLimits = m_nowLimit;
    slot.heldLimits = m_heldLimit;
    slot.rest = m_rest;
    m_published.publish();
    m_publishedCount.fetch_add(1, std::memory_order_release);
}

void Denoise::closeAll() noexcept RB_NONBLOCKING
{
    const float reduction = std::clamp(m_store.number(Reduction), 0.0f, kHighestReductionDb);
    std::ranges::fill(m_gainDb, -reduction);
    std::ranges::fill(m_gain, dbToGain(-reduction));
}

void Denoise::openAll() noexcept RB_NONBLOCKING
{
    std::ranges::fill(m_gainDb, 0.0f);
    std::ranges::fill(m_gain, 1.0f);
}

void Denoise::startLearning() noexcept RB_NONBLOCKING
{
    m_learning = true;
    m_learnFrames = 0;
    m_countedNow = 0;
    m_countedHeld = 0;
    std::ranges::fill(m_countNow, std::uint16_t{0});
    std::ranges::fill(m_countHeld, std::uint16_t{0});
    std::ranges::fill(m_topNow, std::uint8_t{0});
    std::ranges::fill(m_topHeld, std::uint8_t{0});
    m_mostNow.fill(0.0f);
    m_mostHeld.fill(0.0f);
    m_restSum.fill(0.0);
    m_restFrames = 0;
}

void Denoise::dropProfile() noexcept RB_NONBLOCKING
{
    m_learning = false;
    m_haveProfile = false;
    m_profileSeconds = 0.0f;
    std::ranges::fill(m_ceilingNow, kLowestDb);
    std::ranges::fill(m_ceilingHeld, kLowestDb);
    m_nowLimit.fill(0.0f);
    m_heldLimit.fill(0.0f);
    m_rest.fill(0.0f);
    m_followed.fill(0.0f);
}

void Denoise::findCeilings() noexcept RB_NONBLOCKING
{
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        // Each band with the two beside it: three times the levels for the
        // same time, and a tone's skirts under its ceiling as well.
        const std::size_t first = bin > 0 ? bin - 1 : 0;
        const std::size_t last = std::min(bin + 1, kBins - 1);
        std::size_t topNow = 0;
        std::size_t topHeld = 0;
        for (std::size_t band = first; band <= last; ++band) {
            topNow = std::max<std::size_t>(topNow, m_topNow[band]);
            topHeld = std::max<std::size_t>(topHeld, m_topHeld[band]);
        }
        m_ceilingNow[bin] = ceilingOf(m_countNow, kSteps, first, last, topNow, m_countedNow);
        m_ceilingHeld[bin] = ceilingOf(m_countHeld, kSteps, first, last, topHeld, m_countedHeld);
    }
}

float Denoise::restOf(std::size_t octave, float *upper) noexcept RB_NONBLOCKING
{
    const std::size_t first = m_octaveStart[octave];
    const std::size_t count = m_octaveStart[octave + 1] - first;
    if (count == 0) {
        if (upper != nullptr) {
            *upper = 0.0f;
        }
        return 0.0f;
    }
    for (std::size_t band = 0; band < count; ++band) {
        m_scratch[band] = m_held[first + band] - m_ceilingHeld[first + band];
    }
    const auto from = m_scratch.begin();
    const auto end = from + static_cast<std::ptrdiff_t>(count);
    const auto low = static_cast<std::ptrdiff_t>(kRestShare * static_cast<double>(count - 1));
    std::nth_element(from, from + low, end);
    const float rest = m_scratch[static_cast<std::size_t>(low)];
    if (upper != nullptr) {
        // What is over the rest is past it in the scratch, in no order.
        const auto high = static_cast<std::ptrdiff_t>((1.0 - kRestShare) * static_cast<double>(count - 1));
        std::nth_element(from + low, from + high, end);
        *upper = m_scratch[static_cast<std::size_t>(high)];
    }
    return rest;
}

void Denoise::learnFrame() noexcept RB_NONBLOCKING
{
    const std::size_t half = m_learnTarget / 2;
    if (m_learnFrames < half) {
        // The first half counts every band's levels. A held level counts
        // once every frame it holds was heard while learning.
        const bool heldIsTheNoise = m_learnFrames + 1 >= m_heldFrames;
        for (std::size_t bin = 0; bin < kBins; ++bin) {
            const std::size_t now = stepOf(m_level[bin], kSteps);
            countOne(m_countNow[bin * kSteps + now]);
            m_topNow[bin] = std::max(m_topNow[bin], static_cast<std::uint8_t>(now));
            if (heldIsTheNoise) {
                const std::size_t held = stepOf(m_held[bin], kSteps);
                countOne(m_countHeld[bin * kSteps + held]);
                m_topHeld[bin] = std::max(m_topHeld[bin], static_cast<std::uint8_t>(held));
            }
        }
        ++m_countedNow;
        if (heldIsTheNoise) {
            ++m_countedHeld;
        }
    } else {
        // The second half: how far the noise alone strays past the ceilings
        // the first found, in frames that did not help find them, and where
        // its held level rests under them.
        if (m_learnFrames == half) {
            findCeilings();
        }
        std::array<float, kOctaves> now{};
        std::array<float, kOctaves> held{};
        for (std::size_t bin = 0; bin < kBins; ++bin) {
            now[m_octave[bin]] += std::max(0.0f, m_level[bin] - m_ceilingNow[bin] - kEvidenceDb);
            held[m_octave[bin]] += std::max(0.0f, m_held[bin] - m_ceilingHeld[bin] - kEvidenceDb);
        }
        for (std::size_t octave = 0; octave < kOctaves; ++octave) {
            m_mostNow[octave] = std::max(m_mostNow[octave], now[octave]);
            m_mostHeld[octave] = std::max(m_mostHeld[octave], held[octave]);
            m_restSum[octave] += static_cast<double>(restOf(octave, nullptr));
        }
        ++m_restFrames;
    }
    if (++m_learnFrames >= m_learnTarget) {
        finishLearning();
    }
}

void Denoise::finishLearning() noexcept RB_NONBLOCKING
{
    for (std::size_t octave = 0; octave < kOctaves; ++octave) {
        m_nowLimit[octave] = kNowLimitFactor * m_mostNow[octave] + kNowLimitMargin;
        m_heldLimit[octave] = kHeldLimitFactor * m_mostHeld[octave] + kHeldLimitMargin;
        m_rest[octave] =
            m_restFrames > 0 ? static_cast<float>(m_restSum[octave] / static_cast<double>(m_restFrames)) : 0.0f;
    }
    m_followed.fill(0.0f);
    m_profileSeconds = static_cast<float>(static_cast<double>(m_learnFrames * kHop) / m_sampleRate);
    m_haveProfile = true;
    m_learning = false;
    publishProfile(true);
}

void Denoise::measure() noexcept RB_NONBLOCKING
{
    float *const slot = m_ring.data() + m_ringSlot * kBins;
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const float power = 0.5f * (std::norm(m_spectrum[0][bin]) + std::norm(m_spectrum[1][bin])) * m_levelScale;
        const float level = power > kNoPower ? 10.0f * std::log10(power) : kLowestDb;
        m_level[bin] = std::max(level, kLowestDb);
        slot[bin] = m_level[bin];
    }
    m_ringSlot = (m_ringSlot + 1) % m_heldFrames;

    // Each band's median over the frames the ring holds. **Seven, at 44.1
    // and 48 kHz, by a network**: thirteen orderings of two, the same for
    // every band and without a branch, so that bands go eight at a time.
    // The order does not matter to a median, so the ring's slots are read
    // as they lie.
    if (m_heldFrames == 7) {
        const float *const ring = m_ring.data();
        for (std::size_t bin = 0; bin < kBins; ++bin) {
            float p0 = ring[bin];
            float p1 = ring[kBins + bin];
            float p2 = ring[2 * kBins + bin];
            float p3 = ring[3 * kBins + bin];
            float p4 = ring[4 * kBins + bin];
            float p5 = ring[5 * kBins + bin];
            float p6 = ring[6 * kBins + bin];
            order(p0, p5);
            order(p0, p3);
            order(p1, p6);
            order(p2, p4);
            order(p0, p1);
            order(p3, p5);
            order(p2, p6);
            order(p2, p3);
            order(p3, p6);
            order(p4, p5);
            order(p1, p4);
            order(p1, p3);
            order(p3, p4);
            m_held[bin] = p3;
        }
        return;
    }
    // Any other count sorted as it is read: a handful of numbers, so
    // insertion is the quickest sort.
    std::array<float, kMostHeldFrames> sorted{};
    const std::size_t middle = (m_heldFrames - 1) / 2;
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        for (std::size_t frame = 0; frame < m_heldFrames; ++frame) {
            const float value = m_ring[frame * kBins + bin];
            std::size_t at = frame;
            while (at > 0 && sorted[at - 1] > value) {
                sorted[at] = sorted[at - 1];
                --at;
            }
            sorted[at] = value;
        }
        m_held[bin] = sorted[middle];
    }
}

bool Denoise::weighFrame() noexcept RB_NONBLOCKING
{
    std::array<float, kOctaves> now{};
    std::array<float, kOctaves> held{};
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const std::size_t octave = m_octave[bin];
        const float shift = m_followed[octave];
        m_excessNow[bin] = m_level[bin] - (m_ceilingNow[bin] + shift);
        m_excessHeld[bin] = m_held[bin] - (m_ceilingHeld[bin] + shift);
        now[octave] += std::max(0.0f, m_excessNow[bin] - kEvidenceDb);
        held[octave] += std::max(0.0f, m_excessHeld[bin] - kEvidenceDb);
    }
    bool any = false;
    for (std::size_t octave = 0; octave < kOctaves; ++octave) {
        m_heardNow[octave] = now[octave] > m_nowLimit[octave];
        m_heardHeld[octave] = held[octave] > m_heldLimit[octave];
        any = any || m_heardNow[octave] || m_heardHeld[octave];
    }
    return any;
}

void Denoise::followNoise() noexcept RB_NONBLOCKING
{
    // Every few frames is often enough for something that takes seconds,
    // and the rest, an ordering of every octave's bands, costs.
    if (++m_followTick < kFollowEvery) {
        return;
    }
    m_followTick = 0;
    for (std::size_t octave = 0; octave < kOctaves; ++octave) {
        if (m_octaveBands[octave] == 0) {
            continue;
        }
        float upper = 0.0f;
        const float rest = restOf(octave, &upper);
        // **A noise grown is every band a little over; a sound is some bands
        // a lot over.** While three quarters of the octave's bands are less
        // than kFollowUnderDb over the ceilings it follows, nothing stands
        // out of it for long, and the octave goes on following: even while
        // it is heard, since a noise grown at once is heard until followed.
        if (upper - m_followed[octave] >= kFollowUnderDb) {
            continue;
        }
        const float drift = rest - m_rest[octave];
        const float followed = m_followed[octave] + m_followStep * (drift - m_followed[octave]);
        m_followed[octave] = std::clamp(followed, 0.0f, kMostFollowedDb);
    }
}

void Denoise::decideGains() noexcept RB_NONBLOCKING
{
    const float reduction = std::clamp(m_store.number(Reduction), 0.0f, kHighestReductionDb);
    const float threshold = m_store.number(Threshold);
    // A band opens at once and closes over kReleaseSeconds, and never goes
    // further than the reduction.
    const auto release = static_cast<float>(static_cast<double>(reduction) * static_cast<double>(kHop)
                                            / (kReleaseSeconds * m_sampleRate));
    for (std::size_t bin = 0; bin < kBins; ++bin) {
        const std::size_t octave = m_octave[bin];
        float target = -reduction;
        if (m_heardNow[octave] || m_heardHeld[octave]) {
            float excess = kLowestDb;
            if (m_heardNow[octave]) {
                excess = m_excessNow[bin];
            }
            if (m_heardHeld[octave]) {
                excess = std::max(excess, m_excessHeld[bin]);
            }
            target = -reduction * std::clamp((threshold - excess) / kKneeDb, 0.0f, 1.0f);
        }
        const float gainDb = std::clamp(std::max(target, m_gainDb[bin] - release), -reduction, 0.0f);
        // Most bands stay where they were, wide open or turned down in
        // full: their factor is the one they had.
        if (gainDb != m_gainDb[bin]) {
            m_gainDb[bin] = gainDb;
            m_gain[bin] = dbToGain(gainDb);
        }
    }
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

    measure();
    if (m_learning) {
        learnFrame();
    }
    if (m_haveProfile && !m_learning) {
        m_noiseAlone = !weighFrame();
        followNoise();
        decideGains();
    } else {
        m_noiseAlone = false;
        openAll();
    }

    // Back, windowed again, into each channel's overlap-add.
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        for (std::size_t bin = 0; bin < kBins; ++bin) {
            m_spectrum[channel][bin] *= m_gain[bin];
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
        dropProfile();
        publishProfile(false);
    }
    if (m_store.number(Learn) != m_seenLearn) {
        m_seenLearn = m_store.number(Learn);
        startLearning();
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
    std::ranges::fill(m_ring, kLowestDb);
    m_ringSlot = 0;
    m_noiseAlone = false;
    if (m_haveProfile && !m_learning) {
        closeAll();
    } else {
        openAll();
    }
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

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/speechdenoise.h"

#include "dsp/decibel.h"
#include "rt/meter.h"

#include <rnnoise.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

/// RNNoise works in the scale of a 16-bit sample, not of a float one.
constexpr float kModelScale = 32768.0f;

/// **RNNoise from the audio thread.** The compiler cannot see into the
/// library, so it cannot check it: -Wfunction-effects is quietened for
/// these two calls and nothing else. What the check cannot see, the rtsan
/// preset does at run time, and the allocation trap in the tests: neither
/// call allocates, locks or waits — the state is made whole by
/// rnnoise_create() in prepare().
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
#endif
[[nodiscard]] float runModel(DenoiseState *state, float *out, const float *in) noexcept RB_NONBLOCKING
{
    return rnnoise_process_frame(state, out, in);
}

void restartModel(DenoiseState *state) noexcept RB_NONBLOCKING
{
    (void)rnnoise_init(state, nullptr);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

} // namespace

void SpeechDenoise::StateDeleter::operator()(DenoiseState *state) const noexcept
{
    rnnoise_destroy(state);
}

std::vector<ParamDescriptor> SpeechDenoise::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor reduction;
    reduction.id = "reduction";
    reduction.type = ParamType::Float;
    reduction.minimum = ParamValue::ofFloat(0.0f);
    reduction.maximum = ParamValue::ofFloat(kHighestReductionDb);
    reduction.defaultValue = ParamValue::ofFloat(kDefaultReductionDb);
    reduction.unit = ParamUnit::Decibels;
    reduction.smoothingMs = kSmoothingMs;
    descriptors.push_back(std::move(reduction));

    ParamDescriptor mono;
    mono.id = "mono";
    mono.type = ParamType::Bool;
    mono.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(mono));
    return descriptors;
}

SpeechDenoise::SpeechDenoise()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Icon;
    m_info.latencySamples = kLatency;
}

SpeechDenoise::~SpeechDenoise() = default;

std::unique_ptr<Block> SpeechDenoise::make()
{
    return std::make_unique<SpeechDenoise>();
}

std::expected<void, BlockError> SpeechDenoise::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    // The frame the library was built with is the one this block gathers.
    if (static_cast<std::size_t>(rnnoise_get_frame_size()) != kFrame) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_meter = spec.meter;
    m_active = spec.sampleRate == kModelRate;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        if (m_active && !m_states[channel]) {
            m_states[channel].reset(rnnoise_create(nullptr));
            if (!m_states[channel]) {
                return std::unexpected(BlockError::UnsupportedSpec);
            }
        }
        m_gathering[channel].assign(kFrame, 0.0f);
        m_dry[channel].assign(kFrame * (kModelDelayFrames + 1), 0.0f);
        m_wet[channel].assign(kFrame, 0.0f);
        m_ready[channel].assign(kFrame, 0.0f);
    }
    reset();
    return {};
}

void SpeechDenoise::processFrame() noexcept RB_NONBLOCKING
{
    float voice = 0.0f;
    // Folded, both sides are gathered the same and one model runs; the
    // second's output is the first's while it is off or catching up.
    const std::size_t models = m_fold == Fold::Folded ? 1 : kChannels;
    const bool borrow = m_fold == Fold::Folded || m_fold == Fold::Warming;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        // The newest frame of input at the end of the dry history, in the
        // engine's scale.
        std::vector<float> &dry = m_dry[channel];
        std::copy(dry.begin() + kFrame, dry.end(), dry.begin());
        std::transform(m_gathering[channel].begin(), m_gathering[channel].end(), dry.end() - kFrame, [](float sample) {
            return sample / kModelScale;
        });
        if (m_active && channel < models) {
            voice =
                std::max(voice, runModel(m_states[channel].get(), m_wet[channel].data(), m_gathering[channel].data()));
            for (float &sample : m_wet[channel]) {
                sample /= kModelScale;
            }
        }
    }

    if (m_active && borrow) {
        std::ranges::copy(m_wet[0], m_wet[1].begin());
    }

    if (!m_active) {
        // Through, as late as the model would have made it.
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            std::copy_n(m_dry[channel].begin(), kFrame, m_ready[channel].begin());
        }
        m_gate = 0.0f;
        advanceFold();
        return;
    }

    // **The gate looks ahead.** The frame about to be played is the oldest
    // the model has heard; the newer ones it has heard too say whether a
    // word is starting, so the gate is open before the word is.
    std::copy(m_voice.begin() + 1, m_voice.end(), m_voice.begin());
    m_voice.back() = voice;
    const bool speaking = std::ranges::any_of(m_voice, [](float chance) { return chance >= kVoiceThreshold; });
    if (speaking) {
        m_holdLeft = kHoldFrames;
    } else if (m_holdLeft > 0) {
        --m_holdLeft;
    }
    const float gateTo =
        speaking || m_holdLeft > 0 ? 1.0f : std::max(0.0f, m_gate - 1.0f / static_cast<float>(kCloseFrames));
    const float floorTo = dbToGain(-m_store.number(Reduction));

    // Both glide across the frame, so neither is ever a step.
    for (std::size_t n = 0; n < kFrame; ++n) {
        const float along = static_cast<float>(n + 1) / static_cast<float>(kFrame);
        const float open = m_gate + (gateTo - m_gate) * along;
        const float floor = m_floor + (floorTo - m_floor) * along;
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            const float x = m_dry[channel][n];
            const float d = m_wet[channel][n];
            // Speaking: the model's, with the limit's share of what it took
            // out put back. Between words: all of it, down to the limit.
            m_ready[channel][n] = open * (d + floor * (x - d)) + (1.0f - open) * floor * x;
        }
    }
    m_gate = gateTo;
    m_floor = floorTo;
    advanceFold();
}

void SpeechDenoise::advanceFold() noexcept RB_NONBLOCKING
{
    const bool wanted = m_store.number(Mono) >= 0.5f;
    switch (m_fold) {
    case Fold::Apart:
        if (wanted) {
            m_fold = Fold::Folding;
        }
        break;
    case Fold::Folding:
        m_fold = Fold::Settling;
        m_foldFramesLeft = kModelDelayFrames;
        break;
    case Fold::Settling:
        if (!wanted) {
            // Both still running: nothing to catch up.
            m_fold = Fold::Unfolding;
        } else if (--m_foldFramesLeft == 0) {
            m_fold = Fold::Folded;
        }
        break;
    case Fold::Folded:
        if (!wanted) {
            // The second model has not heard anything since it stopped:
            // it starts again, on the fold, and its side borrows the
            // first's output until it has heard as much.
            if (m_states[1]) {
                restartModel(m_states[1].get());
            }
            m_fold = Fold::Warming;
            m_foldFramesLeft = kModelDelayFrames + 1;
        }
        break;
    case Fold::Warming:
        if (wanted) {
            m_fold = Fold::Folded;
        } else if (--m_foldFramesLeft == 0) {
            m_fold = Fold::Unfolding;
        }
        break;
    case Fold::Unfolding:
        m_fold = wanted ? Fold::Folding : Fold::Apart;
        break;
    }
    m_foldFrom = m_foldTo;
    m_foldTo = m_fold == Fold::Apart || m_fold == Fold::Unfolding ? 0.0f : 1.0f;
}

void SpeechDenoise::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    std::array<std::span<float>, kChannels> in;
    std::array<std::span<float>, kChannels> out;
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        in[channel] = context.input.channel(static_cast<std::uint32_t>(channel));
        out[channel] = context.output.channel(static_cast<std::uint32_t>(channel));
    }
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const float left = frame < in[0].size() ? in[0][frame] : 0.0f;
        const float right = frame < in[1].size() ? in[1][frame] : 0.0f;
        for (std::size_t channel = 0; channel < kChannels; ++channel) {
            // Read before written: the graph may hand one buffer for both.
            const float own = channel == 0 ? left : right;
            const float fold =
                m_foldFrom + (m_foldTo - m_foldFrom) * static_cast<float>(m_fill + 1) / static_cast<float>(kFrame);
            // Folded all the way, both sides are the one sample exactly.
            const float mono = 0.5f * (left + right);
            const float sample = fold >= 1.0f ? mono : own + fold * (mono - own);
            if (frame < out[channel].size()) {
                out[channel][frame] = m_ready[channel][m_fill];
            }
            m_gathering[channel][m_fill] = sample * kModelScale;
        }
        if (++m_fill == kFrame) {
            processFrame();
            m_fill = 0;
        }
    }

    if (m_meter != nullptr) {
        m_meter->publish(kStateValue, m_active ? kActive : kWrongRate);
        m_meter->publish(kVoiceValue, m_voice.back());
        m_meter->publish(kGateValue, m_gate);
    }
}

void SpeechDenoise::reset() noexcept RB_NONBLOCKING
{
    for (std::size_t channel = 0; channel < kChannels; ++channel) {
        if (m_states[channel]) {
            restartModel(m_states[channel].get());
        }
        std::ranges::fill(m_gathering[channel], 0.0f);
        std::ranges::fill(m_dry[channel], 0.0f);
        std::ranges::fill(m_wet[channel], 0.0f);
        std::ranges::fill(m_ready[channel], 0.0f);
    }
    m_fill = 0;
    m_voice.fill(0.0f);
    m_holdLeft = 0;
    m_gate = 0.0f;
    m_floor = dbToGain(-m_store.number(Reduction));
    const bool mono = m_store.number(Mono) >= 0.5f;
    m_fold = mono ? Fold::Folded : Fold::Apart;
    m_foldFramesLeft = 0;
    m_foldFrom = mono ? 1.0f : 0.0f;
    m_foldTo = m_foldFrom;
}

void SpeechDenoise::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read once a frame, and glided to across it.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> SpeechDenoise::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue SpeechDenoise::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap SpeechDenoise::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> SpeechDenoise::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/wave.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <utility>

namespace rb::blocks
{

using namespace rb::dsp;

namespace
{

constexpr std::uint32_t kChannels = 2;

/// The step a band-limited edge needs at \a t, a phase a fraction \a dt of
/// a cycle away from a jump (polyBLEP). Zero everywhere else.
[[nodiscard]] double polyBlep(double t, double dt) noexcept RB_NONBLOCKING
{
    if (t < dt) {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

/// The same for a corner, where the slope rather than the value jumps
/// (polyBLAMP): what a triangle needs.
[[nodiscard]] double polyBlamp(double t, double dt) noexcept RB_NONBLOCKING
{
    if (t < dt) {
        const double x = t / dt - 1.0;
        return -x * x * x / 3.0;
    }
    if (t > 1.0 - dt) {
        const double x = (t - 1.0) / dt + 1.0;
        return x * x * x / 3.0;
    }
    return 0.0;
}

[[nodiscard]] double wrapped(double phase) noexcept RB_NONBLOCKING
{
    return phase - std::floor(phase);
}

} // namespace

std::vector<ParamDescriptor> Wave::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor waveform;
    waveform.id = "waveform";
    waveform.type = ParamType::Enum;
    waveform.choices = {std::string{kSine},
                        std::string{kSquare},
                        std::string{kTriangle},
                        std::string{kSaw},
                        std::string{kWhiteNoise},
                        std::string{kPinkNoise}};
    waveform.defaultValue = ParamValue::ofEnum(std::string{kSine});
    descriptors.push_back(std::move(waveform));

    ParamDescriptor frequency;
    frequency.id = "frequency";
    frequency.type = ParamType::Float;
    frequency.minimum = ParamValue::ofFloat(kMinimumFrequency);
    frequency.maximum = ParamValue::ofFloat(kMaximumFrequency);
    frequency.defaultValue = ParamValue::ofFloat(kDefaultFrequency);
    frequency.unit = ParamUnit::Hertz;
    frequency.taper = ParamTaper::Logarithmic;
    frequency.smoothingMs = kSmoothingMs;
    descriptors.push_back(std::move(frequency));

    ParamDescriptor level;
    level.id = "level";
    level.type = ParamType::Float;
    level.minimum = ParamValue::ofFloat(kMinimumLevelDb);
    level.maximum = ParamValue::ofFloat(kMaximumLevelDb);
    level.defaultValue = ParamValue::ofFloat(kDefaultLevelDb);
    level.unit = ParamUnit::Decibels;
    level.taper = ParamTaper::Decibels;
    level.smoothingMs = kSmoothingMs;
    descriptors.push_back(std::move(level));

    ParamDescriptor channels;
    channels.id = "channels";
    channels.type = ParamType::Enum;
    channels.choices = {std::string{kBoth}, std::string{kLeft}, std::string{kRight}};
    channels.defaultValue = ParamValue::ofEnum(std::string{kBoth});
    descriptors.push_back(std::move(channels));

    return descriptors;
}

Wave::Wave()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    // **Listed under Outputs**, next to the devices it is used to test; that
    // it takes nothing in is said by its ports, not by its category.
    m_info.category = BlockCategory::Outputs;
    m_info.inputs = PortSpec::fixed(0);
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Wave::make()
{
    return std::make_unique<Wave>();
}

std::expected<void, BlockError> Wave::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_level.prepare(spec.sampleRate, kSmoothingMs);
    m_frequency.prepare(spec.sampleRate, kSmoothingMs);
    m_left.prepare(spec.sampleRate, kSmoothingMs);
    m_right.prepare(spec.sampleRate, kSmoothingMs);
    settleSmoothers();
    m_phase = 0.0;
    m_pink.fill(0.0f);
    m_prepared = true;
    return {};
}

void Wave::aimSmoothers() noexcept RB_NONBLOCKING
{
    m_level.setTarget(dbToGain(m_store.number(Level)));
    m_frequency.setTarget(m_store.number(Frequency));
    // The choices are both, left, right, counted from nought.
    const auto side = static_cast<int>(m_store.number(Channels));
    m_left.setTarget(side == 2 ? 0.0f : 1.0f);
    m_right.setTarget(side == 1 ? 0.0f : 1.0f);
}

void Wave::settleSmoothers() noexcept RB_NONBLOCKING
{
    m_level.reset(dbToGain(m_store.number(Level)));
    m_frequency.reset(m_store.number(Frequency));
    const auto side = static_cast<int>(m_store.number(Channels));
    m_left.reset(side == 2 ? 0.0f : 1.0f);
    m_right.reset(side == 1 ? 0.0f : 1.0f);
}

float Wave::nextSample(Shape shape, double increment) noexcept RB_NONBLOCKING
{
    const double phase = m_phase;
    const double dt = std::min(increment, 0.5);
    double value = 0.0;

    switch (shape) {
    case Shape::Sine:
        value = std::sin(2.0 * std::numbers::pi * phase);
        break;
    case Shape::Square:
        value = (phase < 0.5 ? 1.0 : -1.0) + polyBlep(phase, dt) - polyBlep(wrapped(phase + 0.5), dt);
        break;
    case Shape::Triangle:
        // Up from −1 to 1 over the first half, down over the second, with
        // its two corners rounded off by just enough not to alias.
        value = 1.0 - 4.0 * std::abs(phase - 0.5);
        value += 4.0 * dt * (polyBlamp(phase, dt) - polyBlamp(wrapped(phase + 0.5), dt));
        break;
    case Shape::Saw:
        value = 2.0 * phase - 1.0 - polyBlep(phase, dt);
        break;
    case Shape::WhiteNoise:
    case Shape::PinkNoise: {
        // xorshift32: fast, and nothing on the audio thread to allocate.
        m_noise ^= m_noise << 13U;
        m_noise ^= m_noise >> 17U;
        m_noise ^= m_noise << 5U;
        const float white = static_cast<float>(m_noise) / 2147483648.0f - 1.0f;
        if (shape == Shape::WhiteNoise) {
            value = white;
            break;
        }
        // Paul Kellet's pink filter, scaled to about the same peak as white.
        auto &b = m_pink;
        b[0] = 0.99886f * b[0] + white * 0.0555179f;
        b[1] = 0.99332f * b[1] + white * 0.0750759f;
        b[2] = 0.96900f * b[2] + white * 0.1538520f;
        b[3] = 0.86650f * b[3] + white * 0.3104856f;
        b[4] = 0.55000f * b[4] + white * 0.5329522f;
        b[5] = -0.7616f * b[5] - white * 0.0168980f;
        const float pink = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f;
        b[6] = white * 0.115926f;
        value = pink * 0.11f;
        break;
    }
    }

    m_phase = wrapped(m_phase + increment);
    return static_cast<float>(value);
}

void Wave::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    aimSmoothers();
    const auto shape = static_cast<Shape>(std::clamp(static_cast<int>(m_store.number(Waveform)), 0, 5));
    const std::uint32_t channels = std::min(context.output.channelCount(), kChannels);
    const std::span<float> left = channels > 0 ? context.output.channel(0) : std::span<float>{};
    const std::span<float> right = channels > 1 ? context.output.channel(1) : std::span<float>{};

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const double increment = static_cast<double>(m_frequency.next()) / m_sampleRate;
        // **Held at full scale**, for the rare sample of pink noise past it:
        // a generator that clipped a device it was testing would be testing
        // itself.
        const float sample = std::clamp(nextSample(shape, increment) * m_level.next(), -1.0f, 1.0f);
        const float toLeft = m_left.next();
        const float toRight = m_right.next();
        if (frame < left.size()) {
            left[frame] = sample * toLeft;
        }
        if (frame < right.size()) {
            right[frame] = sample * toRight;
        }
    }
    // Anything past a stereo pair is silence rather than whatever was there.
    for (std::uint32_t channel = kChannels; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        std::fill(out.begin(), out.begin() + std::min<std::size_t>(context.frames, out.size()), 0.0f);
    }
}

void Wave::reset() noexcept RB_NONBLOCKING
{
    settleSmoothers();
    m_phase = 0.0;
    m_pink.fill(0.0f);
}

void Wave::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Every setting reaches the audio thread this way; the smoothers read
    // them at the top of each cycle, so a change heard mid-drag is a glide.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Wave::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Wave::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Wave::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Wave::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        settleSmoothers();
    }
    return loaded;
}

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/reverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

/// Where each line's input is added, and where the left and the right are
/// taken from. The two outputs are orthogonal to each other, so the sides are
/// as unlike as the network makes them; the input is orthogonal to neither,
/// so the reverb starts on the first trip round rather than the second.
constexpr std::array<float, Reverb::kLines> kInputSigns{1, 1, -1, 1, -1, 1, 1, -1};
constexpr std::array<float, Reverb::kLines> kLeftSigns{1, 1, 1, 1, -1, -1, -1, -1};
constexpr std::array<float, Reverb::kLines> kRightSigns{1, -1, -1, 1, 1, -1, -1, 1};

[[nodiscard]] bool isPrime(std::uint32_t number) noexcept RB_NONBLOCKING
{
    if (number < 2) {
        return false;
    }
    for (std::uint32_t divisor = 2; divisor * divisor <= number; ++divisor) {
        if (number % divisor == 0) {
            return false;
        }
    }
    return true;
}

/// The largest prime at or below \a number (2 at the least): prime lengths
/// share no factor, so no two lines' echoes keep landing together.
[[nodiscard]] std::uint32_t primeAtOrBelow(std::uint32_t number) noexcept RB_NONBLOCKING
{
    while (number > 2 && !isPrime(number)) {
        --number;
    }
    return std::max<std::uint32_t>(number, 2);
}

[[nodiscard]] float flushed(float value) noexcept RB_NONBLOCKING
{
    return std::fabs(value) < Reverb::kFlushBelow ? 0.0f : value;
}

} // namespace

std::vector<ParamDescriptor> Reverb::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor preDelay;
    preDelay.id = "preDelay";
    preDelay.type = ParamType::Float;
    preDelay.minimum = ParamValue::ofFloat(0.0f);
    preDelay.maximum = ParamValue::ofFloat(kLongestPreDelayMs);
    preDelay.defaultValue = ParamValue::ofFloat(kDefaultPreDelayMs);
    preDelay.unit = ParamUnit::Milliseconds;
    preDelay.smoothingMs = kPreDelayGlideMs;
    descriptors.push_back(std::move(preDelay));

    ParamDescriptor size;
    size.id = "size";
    size.type = ParamType::Float;
    size.minimum = ParamValue::ofFloat(0.0f);
    size.maximum = ParamValue::ofFloat(100.0f);
    size.defaultValue = ParamValue::ofFloat(kDefaultSize);
    size.unit = ParamUnit::Percent;
    size.smoothingMs = kSizeGlideMs;
    descriptors.push_back(std::move(size));

    ParamDescriptor decay;
    decay.id = "decay";
    decay.type = ParamType::Float;
    decay.minimum = ParamValue::ofFloat(kShortestDecayMs);
    decay.maximum = ParamValue::ofFloat(kLongestDecayMs);
    decay.defaultValue = ParamValue::ofFloat(kDefaultDecayMs);
    decay.unit = ParamUnit::Milliseconds;
    decay.taper = ParamTaper::Logarithmic;
    decay.smoothingMs = kDecayGlideMs;
    descriptors.push_back(std::move(decay));

    ParamDescriptor damping;
    damping.id = "damping";
    damping.type = ParamType::Float;
    damping.minimum = ParamValue::ofFloat(0.0f);
    damping.maximum = ParamValue::ofFloat(100.0f);
    damping.defaultValue = ParamValue::ofFloat(kDefaultDamping);
    damping.unit = ParamUnit::Percent;
    damping.smoothingMs = kDecayGlideMs;
    descriptors.push_back(std::move(damping));

    ParamDescriptor width;
    width.id = "width";
    width.type = ParamType::Float;
    width.minimum = ParamValue::ofFloat(0.0f);
    width.maximum = ParamValue::ofFloat(100.0f);
    width.defaultValue = ParamValue::ofFloat(kDefaultWidth);
    width.unit = ParamUnit::Percent;
    width.smoothingMs = kGlideMs;
    descriptors.push_back(std::move(width));

    ParamDescriptor mix;
    mix.id = "mix";
    mix.type = ParamType::Float;
    mix.minimum = ParamValue::ofFloat(0.0f);
    mix.maximum = ParamValue::ofFloat(100.0f);
    mix.defaultValue = ParamValue::ofFloat(kDefaultMix);
    mix.unit = ParamUnit::Percent;
    mix.smoothingMs = kGlideMs;
    descriptors.push_back(std::move(mix));
    return descriptors;
}

Reverb::Reverb()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(2, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // **None**: the original goes straight through, and the pre-delay is the
    // effect; see the class comment.
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Reverb::make()
{
    return std::make_unique<Reverb>();
}

std::array<std::uint32_t, Reverb::kLines> Reverb::lengthsFor(float size) const noexcept RB_NONBLOCKING
{
    const double scale = kSmallestScale + (1.0 - kSmallestScale) * static_cast<double>(size) * 0.01;
    std::array<std::uint32_t, kLines> lengths{};
    for (std::size_t line = 0; line < kLines; ++line) {
        const auto samples = static_cast<std::uint32_t>(std::lround(kLineMs[line] * 1.0e-3 * scale * m_sampleRate));
        // Below the room the line has, and a sample short of it so a read
        // between samples always has both.
        lengths[line] = primeAtOrBelow(std::min<std::uint32_t>(samples, static_cast<std::uint32_t>(m_capacity - 2)));
    }
    return lengths;
}

float Reverb::preDelaySamples() const noexcept RB_NONBLOCKING
{
    return static_cast<float>(static_cast<double>(m_store.number(PreDelay)) * 1.0e-3 * m_sampleRate);
}

float Reverb::readLine(std::size_t line, float delay) const noexcept RB_NONBLOCKING
{
    const float *const samples = m_lines.data() + line * m_capacity;
    const auto whole = static_cast<std::size_t>(delay);
    const float fraction = delay - static_cast<float>(whole);
    const std::size_t at = (m_write + m_capacity - whole) % m_capacity;
    if (fraction == 0.0f) {
        return samples[at];
    }
    const std::size_t before = at == 0 ? m_capacity - 1 : at - 1;
    return samples[at] + (samples[before] - samples[at]) * fraction;
}

void Reverb::design() noexcept RB_NONBLOCKING
{
    const double seconds = static_cast<double>(m_decay.current()) * 1.0e-3;
    // The top's decay time as a fraction of the bottom's: 1 at no damping,
    // a tenth at the most.
    const double alpha = 1.0 - 0.9 * static_cast<double>(m_damping.current()) * 0.01;
    const double shaping = 1.0 - 1.0 / (alpha * alpha);
    const double across = static_cast<double>(m_sizeGlide.current());
    double squares = 0.0;
    for (std::size_t line = 0; line < kLines; ++line) {
        const double length = static_cast<double>(m_lengthFrom[line])
            + (static_cast<double>(m_lengthTo[line]) - static_cast<double>(m_lengthFrom[line])) * across;
        // 60 dB in T60 seconds, whatever the line's length.
        const double gain = std::pow(10.0, -3.0 * length / (m_sampleRate * seconds));
        // Jot and Chaigne's pole for a decay time at the top of `alpha` times
        // the bottom's, held below one where the design would pass it.
        const double pole = std::numbers::ln10 / 4.0 * std::log10(gain) * shaping;
        m_gain[line] = static_cast<float>(gain);
        m_pole[line] = static_cast<float>(std::clamp(pole, 0.0, 0.9));
        squares += gain * gain;
    }
    // **As loud whatever the decay**: steady noise comes back from the
    // network at about its own level. A line that keeps g of what goes
    // round holds g^2 / (1 - g^2) of what it is given, so the input is
    // scaled by the square root of the inverse.
    const double meanSquare = squares / static_cast<double>(kLines);
    m_inputGain = static_cast<float>(std::sqrt((1.0 - meanSquare) / meanSquare));
}

void Reverb::settle() noexcept RB_NONBLOCKING
{
    m_preDelay.reset(preDelaySamples());
    m_decay.reset(m_store.number(Decay));
    m_damping.reset(m_store.number(Damping));
    m_width.reset(m_store.number(Width) * 0.01f);
    m_mix.reset(m_store.number(Mix) * 0.01f);
    m_sizeTarget = m_store.number(Size);
    m_lengthTo = lengthsFor(m_sizeTarget);
    m_lengthFrom = m_lengthTo;
    m_sizeGlide.reset(1.0f);
    design();
}

std::expected<void, BlockError> Reverb::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;

    // Room for the longest pre-delay and one sample more, read between two.
    const auto preLength =
        static_cast<std::size_t>(std::ceil(static_cast<double>(kLongestPreDelayMs) * 1.0e-3 * spec.sampleRate)) + 2;
    m_preLine.assign(preLength, 0.0f);
    m_preWrite = 0;

    for (std::size_t index = 0; index < m_diffusers.size(); ++index) {
        Diffuser &diffuser = m_diffusers[index];
        const auto length = std::max<std::size_t>(
            1, static_cast<std::size_t>(std::lround(kDiffuserMs[index] * 1.0e-3 * spec.sampleRate)));
        diffuser.line.assign(length, 0.0f);
        diffuser.write = 0;
        diffuser.gain = kDiffuserGain[index];
    }

    // The longest line at the largest size, and a little more for a read
    // between samples.
    m_capacity = static_cast<std::size_t>(std::ceil(kLineMs.back() * 1.0e-3 * spec.sampleRate)) + 4;
    m_lines.assign(kLines * m_capacity, 0.0f);
    m_write = 0;
    m_state.fill(0.0f);
    m_frameCount = 0;

    m_preDelay.prepare(spec.sampleRate, kPreDelayGlideMs);
    m_sizeGlide.prepare(spec.sampleRate, kSizeGlideMs);
    m_decay.prepare(spec.sampleRate, kDecayGlideMs);
    m_damping.prepare(spec.sampleRate, kDecayGlideMs);
    m_width.prepare(spec.sampleRate, kGlideMs);
    m_mix.prepare(spec.sampleRate, kGlideMs);
    settle();
    m_prepared = true;
    return {};
}

void Reverb::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_preDelay.setTarget(preDelaySamples());
    m_decay.setTarget(m_store.number(Decay));
    m_damping.setTarget(m_store.number(Damping));
    m_width.setTarget(m_store.number(Width) * 0.01f);
    m_mix.setTarget(m_store.number(Mix) * 0.01f);
    // **One move of the lines at a time**: a new size asked for while they
    // move waits for them to arrive, so no line is read at three lengths.
    if (!m_sizeGlide.isSmoothing() && m_store.number(Size) != m_sizeTarget) {
        m_sizeTarget = m_store.number(Size);
        m_lengthFrom = m_lengthTo;
        m_lengthTo = lengthsFor(m_sizeTarget);
        m_sizeGlide.reset(0.0f);
        m_sizeGlide.setTarget(1.0f);
    }

    const std::span<float> inLeft = context.input.channel(0);
    const std::span<float> inRight = context.input.channel(1);
    const std::span<float> outLeft = context.output.channel(0);
    const std::span<float> outRight = context.output.channel(1);
    const float outputScale = 1.0f / std::sqrt(static_cast<float>(kLines));

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const float preDelay = m_preDelay.next();
        const float across = m_sizeGlide.next();
        (void)m_decay.next();
        (void)m_damping.next();
        const float width = m_width.next();
        const float mix = m_mix.next();
        // Counted from the start, not from the cycle, so the quantum never
        // changes when the filters are worked out.
        if (m_frameCount % kCoefficientFrames == 0) {
            design();
        }
        ++m_frameCount;

        const float left = frame < inLeft.size() ? inLeft[frame] : 0.0f;
        const float right = frame < inRight.size() ? inRight[frame] : 0.0f;

        // The two sides folded to one, the pre-delay, then the diffusers.
        m_preLine[m_preWrite] = 0.5f * (left + right);
        const auto preWhole = static_cast<std::size_t>(preDelay);
        const float preFraction = preDelay - static_cast<float>(preWhole);
        const std::size_t preAt = (m_preWrite + m_preLine.size() - preWhole) % m_preLine.size();
        const std::size_t preBefore = preAt == 0 ? m_preLine.size() - 1 : preAt - 1;
        float input = m_preLine[preAt] + (m_preLine[preBefore] - m_preLine[preAt]) * preFraction;
        m_preWrite = m_preWrite + 1 == m_preLine.size() ? 0 : m_preWrite + 1;
        for (Diffuser &diffuser : m_diffusers) {
            const float delayed = diffuser.line[diffuser.write];
            const float kept = input + diffuser.gain * delayed;
            input = delayed - diffuser.gain * kept;
            diffuser.line[diffuser.write] = flushed(kept);
            diffuser.write = diffuser.write + 1 == diffuser.line.size() ? 0 : diffuser.write + 1;
        }

        // What each line gives back, through its loop filter.
        std::array<float, kLines> back{};
        float sum = 0.0f;
        float wetLeft = 0.0f;
        float wetRight = 0.0f;
        for (std::size_t line = 0; line < kLines; ++line) {
            const float length = static_cast<float>(m_lengthFrom[line])
                + (static_cast<float>(m_lengthTo[line]) - static_cast<float>(m_lengthFrom[line])) * across;
            const float read = readLine(line, length);
            const float filtered = m_pole[line] * m_state[line] + m_gain[line] * (1.0f - m_pole[line]) * read;
            m_state[line] = flushed(filtered);
            back[line] = m_state[line];
            sum += back[line];
            wetLeft += kLeftSigns[line] * back[line];
            wetRight += kRightSigns[line] * back[line];
        }
        // The Householder matrix: every line gets its own back, less a
        // quarter of all of them together, which loses nothing.
        const float shared = 2.0f / static_cast<float>(kLines) * sum;
        const float fed = m_inputGain * input;
        for (std::size_t line = 0; line < kLines; ++line) {
            m_lines[line * m_capacity + m_write] = back[line] - shared + kInputSigns[line] * fed;
        }
        m_write = m_write + 1 == m_capacity ? 0 : m_write + 1;

        wetLeft *= outputScale;
        wetRight *= outputScale;
        const float middle = 0.5f * (wetLeft + wetRight);
        const float side = 0.5f * (wetLeft - wetRight) * width;
        // The original and the reverb as shares of one, so that at the
        // reverb alone neither side keeps a trace of its own original.
        if (frame < outLeft.size()) {
            outLeft[frame] = left * (1.0f - mix) + (middle + side) * mix;
        }
        if (frame < outRight.size()) {
            outRight[frame] = right * (1.0f - mix) + (middle - side) * mix;
        }
    }
    if (!m_sizeGlide.isSmoothing()) {
        m_lengthFrom = m_lengthTo;
    }
}

void Reverb::reset() noexcept RB_NONBLOCKING
{
    std::ranges::fill(m_preLine, 0.0f);
    for (Diffuser &diffuser : m_diffusers) {
        std::ranges::fill(diffuser.line, 0.0f);
    }
    std::ranges::fill(m_lines, 0.0f);
    m_state.fill(0.0f);
    m_frameCount = 0;
    settle();
}

void Reverb::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at the top of each cycle: glided to, or moved across.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Reverb::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Reverb::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Reverb::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Reverb::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        // A session opened with its settings is heard with them from the
        // start; gliding is for a hand on a control.
        settle();
    }
    return loaded;
}

} // namespace rb::blocks

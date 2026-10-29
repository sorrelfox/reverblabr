// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/dehum.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <span>
#include <string>
#include <utility>

namespace rb::blocks
{

using namespace rb::dsp;

std::vector<ParamDescriptor> Dehum::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor base;
    base.id = "base";
    base.type = ParamType::Float;
    base.minimum = ParamValue::ofFloat(kMinimumBase);
    base.maximum = ParamValue::ofFloat(kMaximumBase);
    base.defaultValue = ParamValue::ofFloat(kDefaultBase);
    base.unit = ParamUnit::Hertz;
    base.smoothingMs = kSmoothingMs;
    descriptors.push_back(std::move(base));

    ParamDescriptor amount;
    amount.id = "amount";
    amount.type = ParamType::Float;
    amount.minimum = ParamValue::ofFloat(0.0f);
    amount.maximum = ParamValue::ofFloat(kMaximumAmount);
    amount.defaultValue = ParamValue::ofFloat(kDefaultAmount);
    amount.smoothingMs = kSmoothingMs;
    descriptors.push_back(std::move(amount));

    ParamDescriptor harmonics;
    harmonics.id = "harmonics";
    harmonics.type = ParamType::Int;
    harmonics.minimum = ParamValue::ofInt(1);
    harmonics.maximum = ParamValue::ofInt(static_cast<std::int64_t>(kMaximumHarmonics));
    harmonics.defaultValue = ParamValue::ofInt(static_cast<std::int64_t>(kMaximumHarmonics));
    descriptors.push_back(std::move(harmonics));

    ParamDescriptor automatic;
    automatic.id = "auto";
    automatic.type = ParamType::Bool;
    automatic.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(automatic));

    return descriptors;
}

Dehum::Dehum()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Dehum::make()
{
    return std::make_unique<Dehum>();
}

FilterSpec Dehum::notch(double base, std::size_t number, double width) noexcept
{
    const double centre = base * static_cast<double>(number);
    const double wide = width * std::sqrt(static_cast<double>(number));
    return {FilterShape::Notch, centre, centre / wide, 0.0};
}

double Dehum::mixFor(double amount) noexcept
{
    return 1.0 - std::pow(10.0, -amount * kDepthPerStepDb / 20.0);
}

double Dehum::magnitudeDb(double frequency,
                          double base,
                          double amount,
                          std::size_t harmonics,
                          double sampleRate,
                          double width) noexcept
{
    const double mix = mixFor(amount);
    double sum = 0.0;
    for (std::size_t number = 1; number <= std::min(harmonics, kMaximumHarmonics); ++number) {
        const FilterSpec spec = notch(base, number, width);
        if (spec.frequency >= sampleRate * kMaxFrequencyRatio) {
            break;
        }
        // The block mixes the notch with what goes into it: 1 − mix + mix·H.
        const std::complex<double> h = biquadResponse(designBiquad(spec, sampleRate), frequency, sampleRate);
        sum += 20.0 * std::log10(std::max(std::abs(1.0 - mix + mix * h), 1.0e-15));
    }
    return sum;
}

std::size_t Dehum::wantedHarmonics() const noexcept RB_NONBLOCKING
{
    const auto asked =
        static_cast<std::size_t>(std::clamp(m_store.number(Harmonics), 1.0f, static_cast<float>(kMaximumHarmonics)));
    // A harmonic at or past half the rate has nothing to take out.
    std::size_t fits = 0;
    while (fits < asked
           && static_cast<double>(kMaximumBase) * static_cast<double>(fits + 1) < m_sampleRate * kMaxFrequencyRatio) {
        ++fits;
    }
    return fits;
}

std::expected<void, BlockError> Dehum::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_meter = spec.meter;
    m_base.prepare(spec.sampleRate, kSmoothingMs);
    m_width.prepare(spec.sampleRate, kSmoothingMs);
    m_tracker.prepare(spec.sampleRate);
    for (LinearSmoother &mix : m_mix) {
        mix.prepare(spec.sampleRate, kSmoothingMs);
    }
    settleSmoothers();
    for (std::array<Svf, kMaximumHarmonics> &channel : m_filters) {
        for (Svf &filter : channel) {
            filter.reset();
        }
    }
    m_prepared = true;
    return {};
}

void Dehum::design(double base, double width) noexcept RB_NONBLOCKING
{
    for (std::size_t index = 0; index < kMaximumHarmonics; ++index) {
        const FilterSpec spec = notch(base, index + 1, width);
        if (spec.frequency < m_sampleRate * kMaxFrequencyRatio) {
            m_notches[index] = designSvf(spec, m_sampleRate);
        }
    }
}

void Dehum::aimSmoothers() noexcept RB_NONBLOCKING
{
    if (m_tracking) {
        // **Base is where Auto starts looking**: moved, the tracker looks
        // there, and waits there while it has nothing locked.
        const float base = m_store.number(Base);
        if (base != m_aimed) {
            m_aimed = base;
            m_tracker.aim(base);
        }
    } else {
        m_base.setTarget(m_store.number(Base));
    }
    m_width.setTarget(static_cast<float>(m_tracking ? kAutoWidthHz : kWidthHz));
    const auto mix = static_cast<float>(mixFor(m_store.number(Amount)));
    const std::size_t harmonics = wantedHarmonics();
    for (std::size_t index = 0; index < kMaximumHarmonics; ++index) {
        m_mix[index].setTarget(index < harmonics ? mix : 0.0f);
    }
}

void Dehum::settleSmoothers() noexcept RB_NONBLOCKING
{
    m_tracking = m_store.number(Auto) > 0.5f;
    m_aimed = m_store.number(Base);
    m_tracker.reset(m_aimed);
    m_designCount = 0;
    m_base.reset(m_store.number(Base));
    m_width.reset(static_cast<float>(m_tracking ? kAutoWidthHz : kWidthHz));
    const auto mix = static_cast<float>(mixFor(m_store.number(Amount)));
    const std::size_t harmonics = wantedHarmonics();
    for (std::size_t index = 0; index < kMaximumHarmonics; ++index) {
        m_mix[index].reset(index < harmonics ? mix : 0.0f);
    }
    design(m_base.current(), m_width.current());
}

void Dehum::followAuto() noexcept RB_NONBLOCKING
{
    const bool wanted = m_store.number(Auto) > 0.5f;
    if (wanted == m_tracking) {
        return;
    }
    m_tracking = wanted;
    m_designCount = 0;
    if (m_tracking) {
        // From where the notches are, so switching Auto on moves nothing
        // until there is a hum to follow.
        m_aimed = m_store.number(Base);
        m_tracker.reset(m_base.current());
        m_tracker.aim(m_aimed);
    } else {
        // Back to Base, gliding from where Auto had the notches.
        m_base.reset(static_cast<float>(m_tracker.estimate()));
    }
}

void Dehum::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    followAuto();
    aimSmoothers();
    const std::uint32_t channels = std::min(context.output.channelCount(), kChannels);
    const std::uint32_t inputs = std::min(context.input.channelCount(), kChannels);

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        if (m_tracking) {
            // Both channels mixed: hum is usually in both, and where it is
            // in one it is still the same hum.
            float mixed = 0.0f;
            for (std::uint32_t channel = 0; channel < inputs; ++channel) {
                const std::span<float> in = context.input.channel(channel);
                mixed += frame < in.size() ? in[frame] : 0.0f;
            }
            m_tracker.push(inputs > 0 ? mixed / static_cast<float>(inputs) : 0.0f);
            (void)m_width.next();
            // A few hundredths of a hertz at most between moves, so every
            // kAutoDesignFrames samples is often enough, and a tenth of the
            // work of designing every sample.
            if (++m_designCount >= kAutoDesignFrames) {
                m_designCount = 0;
                design(m_tracker.estimate(), m_width.current());
            }
        } else if (m_base.isSmoothing() || m_width.isSmoothing()) {
            (void)m_base.next();
            (void)m_width.next();
            design(m_base.current(), m_width.current());
        }
        std::array<float, kMaximumHarmonics> mixes{};
        for (std::size_t index = 0; index < kMaximumHarmonics; ++index) {
            mixes[index] = m_mix[index].next();
        }
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> out = context.output.channel(channel);
            if (frame >= out.size()) {
                continue;
            }
            float sample = 0.0f;
            if (channel < context.input.channelCount()) {
                const std::span<float> in = context.input.channel(channel);
                sample = frame < in.size() ? in[frame] : 0.0f;
            }
            for (std::size_t index = 0; index < kMaximumHarmonics; ++index) {
                // **Every notch runs all the time**, even at no amount, so
                // one being turned up or switched back on already has the
                // signal's history rather than ringing up from rest as it
                // fades in. At a mix of 0 the sample still passes as it
                // came, bit for bit.
                const float notched = m_filters[channel][index].process(sample, m_notches[index]);
                sample += (notched - sample) * mixes[index];
            }
            out[frame] = sample;
        }
    }
    for (std::uint32_t channel = kChannels; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        std::fill(out.begin(), out.begin() + std::min<std::size_t>(context.frames, out.size()), 0.0f);
    }
    if (m_meter != nullptr) {
        m_meter->publish(kBaseValue, static_cast<float>(m_tracking ? m_tracker.estimate() : m_base.current()));
        m_meter->publish(kLockedValue, m_tracking ? (m_tracker.locked() ? 1.0f : 0.0f) : -1.0f);
    }
}

void Dehum::reset() noexcept RB_NONBLOCKING
{
    settleSmoothers();
    for (std::array<Svf, kMaximumHarmonics> &channel : m_filters) {
        for (Svf &filter : channel) {
            filter.reset();
        }
    }
}

void Dehum::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Dehum::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Dehum::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Dehum::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Dehum::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        settleSmoothers();
    }
    return loaded;
}

} // namespace rb::blocks

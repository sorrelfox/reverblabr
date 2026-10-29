// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/cutofffilter.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>

namespace rb::blocks
{

using namespace rb::dsp;

std::vector<ParamDescriptor> CutoffFilter::makeDescriptors(const CutoffFilterKind &kind)
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor cutoff;
    cutoff.id = "cutoff";
    cutoff.type = ParamType::Float;
    cutoff.minimum = ParamValue::ofFloat(kind.minimumCutoff);
    cutoff.maximum = ParamValue::ofFloat(kind.maximumCutoff);
    cutoff.defaultValue = ParamValue::ofFloat(kind.defaultCutoff);
    cutoff.unit = ParamUnit::Hertz;
    cutoff.taper = ParamTaper::Logarithmic;
    cutoff.smoothingMs = kCutoffSmoothingMs;
    descriptors.push_back(std::move(cutoff));

    ParamDescriptor slope;
    slope.id = "slope";
    slope.type = ParamType::Enum;
    slope.choices = {std::string{kSlope12}, std::string{kSlope24}};
    slope.defaultValue = ParamValue::ofEnum(std::string{kSlope12});
    descriptors.push_back(std::move(slope));

    return descriptors;
}

CutoffFilter::CutoffFilter(const CutoffFilterKind &kind)
    : m_shape(kind.shape)
    , m_descriptors(makeDescriptors(kind))
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kind.typeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

double
CutoffFilter::magnitudeDb(FilterShape shape, double frequency, double cutoff, bool steep, double sampleRate) noexcept
{
    if (!steep) {
        return filterMagnitudeDb({shape, cutoff, kButterworthQ, 0.0}, frequency, sampleRate);
    }
    return filterMagnitudeDb({shape, cutoff, kButterworth4Q1, 0.0}, frequency, sampleRate)
        + filterMagnitudeDb({shape, cutoff, kButterworth4Q2, 0.0}, frequency, sampleRate);
}

std::expected<void, BlockError> CutoffFilter::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_octaves.prepare(spec.sampleRate, kCutoffSmoothingMs);
    m_steep.prepare(spec.sampleRate, kSlopeFadeMs);
    settleSmoothers();
    design(m_octaves.current());
    resetFilters();
    m_prepared = true;
    return {};
}

void CutoffFilter::design(float octaves) noexcept RB_NONBLOCKING
{
    const double cutoff = std::exp2(static_cast<double>(octaves));
    m_gentle = designSvf({m_shape, cutoff, kButterworthQ, 0.0}, m_sampleRate);
    m_steepFirst = designSvf({m_shape, cutoff, kButterworth4Q1, 0.0}, m_sampleRate);
    m_steepSecond = designSvf({m_shape, cutoff, kButterworth4Q2, 0.0}, m_sampleRate);
}

void CutoffFilter::aimSmoothers() noexcept RB_NONBLOCKING
{
    m_octaves.setTarget(std::log2(m_store.number(Cutoff)));
    // The choices are 12 and 24 dB, counted from nought.
    m_steep.setTarget(m_store.number(Slope) != 0.0f ? 1.0f : 0.0f);
}

void CutoffFilter::settleSmoothers() noexcept RB_NONBLOCKING
{
    m_octaves.reset(std::log2(m_store.number(Cutoff)));
    m_steep.reset(m_store.number(Slope) != 0.0f ? 1.0f : 0.0f);
}

void CutoffFilter::resetFilters() noexcept RB_NONBLOCKING
{
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        m_gentleFilters[channel].reset();
        m_steepFirstFilters[channel].reset();
        m_steepSecondFilters[channel].reset();
    }
}

void CutoffFilter::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    aimSmoothers();
    const std::uint32_t channels = std::min(context.output.channelCount(), kChannels);

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // **Worked out afresh only while the cutoff moves**: three tangents
        // a sample for the length of a glide, none once it has arrived.
        if (m_octaves.isSmoothing()) {
            design(m_octaves.next());
        }
        const float steep = m_steep.next();
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> out = context.output.channel(channel);
            if (frame >= out.size()) {
                continue;
            }
            float in = 0.0f;
            if (channel < context.input.channelCount()) {
                const std::span<float> from = context.input.channel(channel);
                in = frame < from.size() ? from[frame] : 0.0f;
            }
            // Both slopes run all the time, so switching between them is a
            // fade between two filters that already have the signal's
            // history, not a filter starting from nothing.
            const float gentle = m_gentleFilters[channel].process(in, m_gentle);
            const float steeper = m_steepSecondFilters[channel].process(
                m_steepFirstFilters[channel].process(in, m_steepFirst), m_steepSecond);
            out[frame] = gentle + (steeper - gentle) * steep;
        }
    }
    // Anything past a stereo pair is silence rather than whatever was there.
    for (std::uint32_t channel = kChannels; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        std::fill(out.begin(), out.begin() + std::min<std::size_t>(context.frames, out.size()), 0.0f);
    }
}

void CutoffFilter::reset() noexcept RB_NONBLOCKING
{
    settleSmoothers();
    design(m_octaves.current());
    resetFilters();
}

void CutoffFilter::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Both settings reach the audio thread this way; the smoothers read them
    // at the top of each cycle, so a change heard mid-drag is a glide.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> CutoffFilter::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue CutoffFilter::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap CutoffFilter::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> CutoffFilter::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        settleSmoothers();
        design(m_octaves.current());
    }
    return loaded;
}

} // namespace rb::blocks

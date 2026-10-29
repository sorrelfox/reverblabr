// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/peakrmsmeter.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

} // namespace

std::vector<ParamDescriptor> PeakRmsMeter::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor hold;
    hold.id = "peakHold";
    hold.type = ParamType::Enum;
    hold.choices = {std::string{PeakRmsMeter::kHoldOff},
                    std::string{PeakRmsMeter::kHold1s},
                    std::string{PeakRmsMeter::kHold2s},
                    std::string{PeakRmsMeter::kHold5s},
                    std::string{PeakRmsMeter::kHoldForever}};
    hold.defaultValue = ParamValue::ofEnum(std::string{PeakRmsMeter::kHold2s});
    descriptors.push_back(std::move(hold));

    ParamDescriptor tray;
    tray.id = "showInTray";
    tray.type = ParamType::Bool;
    tray.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(tray));
    return descriptors;
}

PeakRmsMeter::PeakRmsMeter()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Meters;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Meter;
    // **Nothing at all.** A meter that delayed the signal would have to be
    // compensated for everywhere one was dropped in.
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> PeakRmsMeter::make()
{
    return std::make_unique<PeakRmsMeter>();
}

std::expected<void, BlockError> PeakRmsMeter::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    m_window = static_cast<std::uint32_t>(std::lround(static_cast<double>(kWindowMs) * 1.0e-3 * spec.sampleRate));
    m_window = std::max(m_window, 1U);
    m_squares.assign(static_cast<std::size_t>(kChannels) * m_window, 0.0f);
    m_meter = spec.meter;
    reset();
    return {};
}

void PeakRmsMeter::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        for (std::uint32_t channel = 0; channel < channels && channel < kChannels; ++channel) {
            const std::span<float> in = context.input.channel(channel);
            const std::span<float> out = context.output.channel(channel);
            const float sample = frame < in.size() ? in[frame] : 0.0f;
            if (frame < out.size()) {
                // **Sample for sample.** A meter is something to look at,
                // not something in the way.
                out[frame] = sample;
            }

            const std::size_t slot = static_cast<std::size_t>(channel) * m_window + m_write;
            // The square leaving the window comes off the sum as the new one
            // goes on, so the sum is of exactly the last window of samples
            // however long the block runs.
            m_sum[channel] += static_cast<double>(sample) * sample - static_cast<double>(m_squares[slot]);
            m_squares[slot] = sample * sample;
        }
        m_write = m_write + 1 == m_window ? 0 : m_write + 1;
        m_filled = std::min(m_filled + 1, m_window);
    }

    if (m_meter != nullptr) {
        m_meter->publish(kRmsLeftValue, rms(0));
        m_meter->publish(kRmsRightValue, rms(1));
    }
}

float PeakRmsMeter::rms(std::uint32_t channel) const noexcept RB_NONBLOCKING
{
    if (channel >= kChannels || m_filled == 0) {
        return 0.0f;
    }
    // Over what has been seen, not over the whole window, so the first
    // moments of a session read the level rather than a fraction of it.
    const double mean = std::max(0.0, m_sum[channel]) / static_cast<double>(m_filled);
    return static_cast<float>(std::sqrt(mean));
}

void PeakRmsMeter::reset() noexcept RB_NONBLOCKING
{
    std::ranges::fill(m_squares, 0.0f);
    m_sum.fill(0.0);
    m_write = 0;
    m_filled = 0;
}

void PeakRmsMeter::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> PeakRmsMeter::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue PeakRmsMeter::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap PeakRmsMeter::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> PeakRmsMeter::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

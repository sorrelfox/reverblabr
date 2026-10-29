// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/declick.h"

#include "dsp/decibel.h"

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

/// The median absolute deviation of normally spread numbers, over their
/// spread: turns a median into a standard deviation.
constexpr double kMadToSigma = 1.4826;

} // namespace

std::vector<ParamDescriptor> Declick::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor threshold;
    threshold.id = "threshold";
    threshold.type = ParamType::Float;
    threshold.minimum = ParamValue::ofFloat(kMinimumThreshold);
    threshold.maximum = ParamValue::ofFloat(kMaximumThreshold);
    threshold.defaultValue = ParamValue::ofFloat(kDefaultThreshold);
    threshold.unit = ParamUnit::Decibels;
    descriptors.push_back(std::move(threshold));
    return descriptors;
}

Declick::Declick()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // Three quarters of a frame, which depends on the rate: set in
    // prepare().
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Declick::make()
{
    return std::make_unique<Declick>();
}

std::expected<void, BlockError> Declick::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_meter = spec.meter;
    // A power of two at least kFrameMs long, and never so short that the
    // middle half lacks a model's worth of sound around it.
    m_frameSize = 16 * kOrder;
    while (static_cast<double>(m_frameSize) < kFrameMs * 1.0e-3 * spec.sampleRate) {
        m_frameSize *= 2;
    }
    m_hop = m_frameSize / 2;
    const std::size_t quarter = m_frameSize / 4;
    // A click starting anywhere in the middle half still ends with the
    // model's order to spare before the frame does.
    m_longestClick =
        std::min<std::size_t>(quarter - kOrder - 2 * kMargin - 1,
                              static_cast<std::size_t>(std::lround(kMaxClickMs * 1.0e-3 * spec.sampleRate)));
    m_info.latencySamples = static_cast<std::uint32_t>(3 * quarter);

    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        m_frames[channel].assign(m_frameSize, 0.0f);
        m_out[channel].assign(m_hop, 0.0f);
    }
    m_window.resize(m_frameSize);
    for (std::size_t n = 0; n < m_frameSize; ++n) {
        m_window[n] = static_cast<float>(
            0.5
            - 0.5
                * std::cos(2.0 * std::numbers::pi * (static_cast<double>(n) + 0.5) / static_cast<double>(m_frameSize)));
    }
    m_error.assign(m_frameSize, 0.0);
    m_matched.assign(m_frameSize, 0.0);
    m_sorted.assign(m_frameSize, 0.0);
    m_model.prepare(kOrder, m_longestClick + 2 * kMargin);
    m_fill.prepare(kOrder, m_longestClick + 2 * kMargin);
    m_fillWindow.assign(m_frameSize, 0.0f);
    m_clicks = 0;
    reset();
    return {};
}

void Declick::errors(std::span<const float> frame, std::size_t from, std::size_t to) noexcept RB_NONBLOCKING
{
    const std::size_t p = kOrder;
    const std::size_t size = m_frameSize;
    const std::span<const double> b = m_model.filter();
    // The prediction error, and the same filtered back through the model:
    // an impulse's error, spread over the order after it, gathered back
    // onto where it was. The error is needed an order further on than the
    // filtered error is.
    const std::size_t errorFrom = std::max(from, p);
    const std::size_t errorTo = std::min(to + p, size);
    for (std::size_t n = errorFrom; n < errorTo; ++n) {
        m_error[n] = m_model.error(frame, n);
    }
    const std::size_t matchedTo = std::min(to, size - p);
    for (std::size_t n = errorFrom; n < matchedTo; ++n) {
        double sum = 0.0;
        for (std::size_t k = 0; k <= p; ++k) {
            sum += b[k] * m_error[n + k];
        }
        m_matched[n] = sum;
    }
}

std::size_t Declick::silentRun(std::span<const float> frame, std::size_t n) const noexcept RB_NONBLOCKING
{
    // **A dropout**: the sound gone to nothing — exact silence, or as near
    // as a converter's own noise — for a few samples, with sound on both
    // sides. Its middle is as predictable as silence, so the error alone
    // only finds its ends.
    const std::size_t p = kOrder;
    std::size_t end = n;
    while (end < m_frameSize - p && std::fabs(frame[end]) <= kSilence) {
        ++end;
    }
    const std::size_t length = end - n;
    if (length < kShortestDropout || length > m_longestClick || n < p || end + p > m_frameSize) {
        return 0;
    }
    const auto loud = [&frame](std::size_t from, std::size_t to) {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i) {
            sum += static_cast<double>(frame[i]) * frame[i];
        }
        return sum / static_cast<double>(to - from) > kAroundDropout * kAroundDropout;
    };
    return loud(n - p, n) && loud(end, end + p) ? length : 0;
}

bool Declick::found(std::span<const float> frame, std::size_t n, std::size_t &end) const noexcept RB_NONBLOCKING
{
    if (std::fabs(m_matched[n]) > m_limit) {
        end = n;
        return true;
    }
    if (const std::size_t run = silentRun(frame, n); run > 0) {
        end = n + run - 1;
        return true;
    }
    return false;
}

std::size_t Declick::measure(std::vector<float> &frame, std::size_t seed, std::size_t seedEnd) noexcept RB_NONBLOCKING
{
    // **What belongs to one click**: whatever stands out within kJoin of
    // what already does, as far as the longest click reaches. A crackle's
    // burst stands out all through; a dropout is one silent run.
    const std::size_t p = kOrder;
    const std::size_t stop = m_frameSize - p;
    const std::size_t reach = std::min(seed + m_longestClick, stop);
    std::size_t end = seedEnd;
    for (std::size_t j = end + 1; j < reach && j <= end + kJoin; ++j) {
        std::size_t hitEnd = j;
        if (found(frame, j, hitEnd)) {
            end = std::min(hitEnd, reach - 1);
            j = end;
        }
    }

    const std::size_t start = seed - kMargin;
    const std::size_t length = end + kMargin - start + 1;
    // **Mended from a model of the sound around the click, not of the
    // click**: the model is fitted again with the click weighted out of
    // it, then the samples that fit it best are found given everything on
    // both sides (least-squares AR interpolation).
    std::copy(m_window.begin(), m_window.end(), m_fillWindow.begin());
    std::fill(m_fillWindow.begin() + static_cast<std::ptrdiff_t>(start),
              m_fillWindow.begin() + static_cast<std::ptrdiff_t>(std::min(start + length, m_frameSize)),
              0.0f);
    dsp::Autoregressive &fill = m_fill.fit(frame, m_fillWindow) ? m_fill : m_model;
    if (end + kMargin + p < m_frameSize && fill.interpolate(frame, start, length)) {
        ++m_clicks;
        errors(frame, start > p ? start - p : p, start + length + p);
    }
    return end + kMargin;
}

void Declick::mend(std::uint32_t channel) noexcept RB_NONBLOCKING
{
    std::vector<float> &frame = m_frames[channel];
    const std::size_t size = m_frameSize;
    const std::size_t quarter = size / 4;
    const std::size_t p = kOrder;

    if (m_model.fit(frame, m_window)) {
        errors(frame, p, size);
        std::size_t count = 0;
        for (std::size_t n = p; n + p < size; ++n) {
            m_sorted[count++] = std::fabs(m_matched[n]);
        }
        const auto middle = m_sorted.begin() + static_cast<std::ptrdiff_t>(count / 2);
        std::nth_element(m_sorted.begin(), middle, m_sorted.begin() + static_cast<std::ptrdiff_t>(count));
        // Never finer than a thousandth of the frame's own level: a pure
        // tone is predicted to the last bit, and its rounding is not a
        // click.
        double level = 0.0;
        for (const float sample : frame) {
            level += static_cast<double>(sample) * sample;
        }
        level = std::sqrt(level / static_cast<double>(size));
        const double spread = std::max({kMadToSigma * *middle, kLevelFloor * level, 1.0e-9});
        m_limit = spread * static_cast<double>(dbToGain(m_store.number(Threshold) + kThresholdFloorDb));

        // Clicks that start in the middle half, one at a time.
        std::size_t n = quarter;
        while (n < 3 * quarter) {
            std::size_t end = n;
            if (!found(frame, n, end)) {
                ++n;
                continue;
            }
            n = measure(frame, n, end) + 1;
        }
    }

    std::copy(frame.begin() + static_cast<std::ptrdiff_t>(quarter),
              frame.begin() + static_cast<std::ptrdiff_t>(3 * quarter),
              m_out[channel].begin());
    std::copy(frame.begin() + static_cast<std::ptrdiff_t>(m_hop), frame.end(), frame.begin());
}

void Declick::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    std::array<std::span<float>, kChannels> in;
    std::array<std::span<float>, kChannels> out;
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        in[channel] = context.input.channel(channel);
        out[channel] = context.output.channel(channel);
    }
    const std::size_t newest = m_frameSize - m_hop;
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            if (frame < out[channel].size()) {
                out[channel][frame] = m_out[channel][m_position];
            }
            m_frames[channel][newest + m_position] = frame < in[channel].size() ? in[channel][frame] : 0.0f;
        }
        if (++m_position == m_hop) {
            for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
                mend(channel);
            }
            m_position = 0;
        }
    }
    if (m_meter != nullptr) {
        m_meter->publish(kClicksValue, static_cast<float>(m_clicks));
    }
}

void Declick::reset() noexcept RB_NONBLOCKING
{
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        std::fill(m_frames[channel].begin(), m_frames[channel].end(), 0.0f);
        std::fill(m_out[channel].begin(), m_out[channel].end(), 0.0f);
    }
    m_position = 0;
}

void Declick::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // Read at the next frame; nothing to smooth, since it only decides what
    // is mended.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Declick::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Declick::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Declick::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Declick::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

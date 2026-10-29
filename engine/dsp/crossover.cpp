// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/crossover.h"

#include <algorithm>
#include <cmath>

namespace rb::dsp
{

namespace
{

/// A glide closer than this to where it is going, in octaves, is there.
constexpr double kArrivedOctaves = 1.0e-6;

/// The highest either split may go, as a fraction of the rate: a little
/// under where a filter is still sound, so the upper split's all-pass and
/// its high half have room above them.
constexpr double kHighestSplitRatio = 0.45;

} // namespace

CrossoverSplits clampCrossoverSplits(double lowHz, double highHz, double sampleRate) noexcept RB_NONBLOCKING
{
    const double highest = sampleRate * kHighestSplitRatio;
    CrossoverSplits splits;
    splits.highHz = std::clamp(highHz, kMinFrequencyHz * kMinCrossoverSpacing, highest);
    splits.lowHz = std::clamp(lowHz, kMinFrequencyHz, highest / kMinCrossoverSpacing);
    // Too close: the upper one moves up if it can, and the lower one down if
    // the upper is already as high as it goes.
    if (splits.highHz < splits.lowHz * kMinCrossoverSpacing) {
        splits.highHz = std::min(splits.lowHz * kMinCrossoverSpacing, highest);
        splits.lowHz = std::min(splits.lowHz, splits.highHz / kMinCrossoverSpacing);
    }
    return splits;
}

void ThreeBandCrossover::prepare(double sampleRate, std::uint32_t channels)
{
    m_sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    m_channels.assign(channels, Channel{});
    // A one-pole glide in octaves, 99 % of the way in kCrossoverGlideMs.
    const double timeConstant = kCrossoverGlideMs / 1000.0 / std::log(100.0);
    m_glideStep = 1.0 - std::exp(-1.0 / (timeConstant * m_sampleRate));
    // Whatever was asked for before the rate was known, brought inside it.
    setSplits(m_askedLowHz, m_askedHighHz);
    reset();
}

void ThreeBandCrossover::setSplits(double lowHz, double highHz) noexcept RB_NONBLOCKING
{
    m_askedLowHz = lowHz;
    m_askedHighHz = highHz;
    const CrossoverSplits splits = clampCrossoverSplits(lowHz, highHz, m_sampleRate);
    m_lowTarget = std::log2(splits.lowHz);
    m_highTarget = std::log2(splits.highHz);
}

void ThreeBandCrossover::reset() noexcept RB_NONBLOCKING
{
    for (Channel &channel : m_channels) {
        channel = Channel{};
    }
    m_lowOctave = m_lowTarget;
    m_highOctave = m_highTarget;
    designBoth();
}

CrossoverSplits ThreeBandCrossover::splits() const noexcept RB_NONBLOCKING
{
    return CrossoverSplits{std::exp2(m_lowOctave), std::exp2(m_highOctave)};
}

void ThreeBandCrossover::design(Split &split, double hz) const noexcept RB_NONBLOCKING
{
    split.low = designSvf({FilterShape::LowPass, hz, kButterworthQ, 0.0}, m_sampleRate);
    split.high = designSvf({FilterShape::HighPass, hz, kButterworthQ, 0.0}, m_sampleRate);
    split.allPass = designSvf({FilterShape::AllPass, hz, kButterworthQ, 0.0}, m_sampleRate);
}

void ThreeBandCrossover::designBoth() noexcept RB_NONBLOCKING
{
    design(m_lowSplit, std::exp2(m_lowOctave));
    design(m_highSplit, std::exp2(m_highOctave));
}

bool ThreeBandCrossover::glide() noexcept RB_NONBLOCKING
{
    const bool lowMoving = std::abs(m_lowTarget - m_lowOctave) > kArrivedOctaves;
    const bool highMoving = std::abs(m_highTarget - m_highOctave) > kArrivedOctaves;
    if (!lowMoving && !highMoving) {
        if (m_lowOctave != m_lowTarget || m_highOctave != m_highTarget) {
            // The last hair of the way, so a glide that has arrived is
            // exactly where it was sent and the filters stop being redone.
            m_lowOctave = m_lowTarget;
            m_highOctave = m_highTarget;
            return true;
        }
        return false;
    }
    m_lowOctave += (m_lowTarget - m_lowOctave) * m_glideStep;
    m_highOctave += (m_highTarget - m_highOctave) * m_glideStep;
    return true;
}

void ThreeBandCrossover::process(const AudioBlock &input,
                                 const AudioBlock &low,
                                 const AudioBlock &mid,
                                 const AudioBlock &high,
                                 std::uint32_t frames) noexcept RB_NONBLOCKING
{
    const auto channels = static_cast<std::uint32_t>(std::min<std::size_t>(
        {m_channels.size(), input.channelCount(), low.channelCount(), mid.channelCount(), high.channelCount()}));
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        if (glide()) {
            designBoth();
        }
        for (std::uint32_t index = 0; index < channels; ++index) {
            const std::span<float> in = input.channel(index);
            const std::span<float> lowOut = low.channel(index);
            const std::span<float> midOut = mid.channel(index);
            const std::span<float> highOut = high.channel(index);
            if (frame >= in.size() || frame >= lowOut.size() || frame >= midOut.size() || frame >= highOut.size()) {
                continue;
            }
            Channel &channel = m_channels[index];
            const float x = in[frame];

            // The lower split: the low band, and the rest.
            const float below =
                channel.lowSplitLow2.process(channel.lowSplitLow1.process(x, m_lowSplit.low), m_lowSplit.low);
            const float above =
                channel.lowSplitHigh2.process(channel.lowSplitHigh1.process(x, m_lowSplit.high), m_lowSplit.high);
            // The upper split, of the rest.
            const float middle =
                channel.highSplitLow2.process(channel.highSplitLow1.process(above, m_highSplit.low), m_highSplit.low);
            const float top = channel.highSplitHigh2.process(channel.highSplitHigh1.process(above, m_highSplit.high),
                                                             m_highSplit.high);
            // The low band turned by the upper split, as the other two were.
            lowOut[frame] = channel.lowBandAllPass.process(below, m_highSplit.allPass);
            midOut[frame] = middle;
            highOut[frame] = top;
        }
    }
}

} // namespace rb::dsp

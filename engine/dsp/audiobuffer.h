// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audioblock.h"
#include "rt/realtime.h"

#include <cstdint>
#include <span>
#include <vector>

namespace rb::dsp
{

/// Planar storage for a number of channels, and the AudioBlock views onto it.
///
/// This is the owning counterpart of AudioBlock: the graph preallocates one
/// per signal path in prepare(), and the offline test runner uses it to hold
/// whole signals. Only resize() and the constructor allocate; everything the
/// audio thread touches is a view.
///
/// Channels are stored one after another, each padded to its own allocation
/// of \a frames samples, so a channel's samples are contiguous and a block
/// view can be handed out without copying.
class AudioBuffer
{
public:
    AudioBuffer() = default;

    /// Allocates \a channelCount channels of \a frames samples, all zero.
    AudioBuffer(std::uint32_t channelCount, std::uint32_t frames)
    {
        resize(channelCount, frames);
    }

    /// Allocates and zeroes. Not real-time safe; call it from prepare().
    void resize(std::uint32_t channelCount, std::uint32_t frames)
    {
        m_samples.assign(static_cast<std::size_t>(channelCount) * frames, 0.0f);
        m_channels.resize(channelCount);
        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            m_channels[channel] = m_samples.data() + static_cast<std::size_t>(channel) * frames;
        }
        m_channelCount = channelCount;
        m_frames = frames;
    }

    [[nodiscard]] std::uint32_t channelCount() const noexcept RB_NONBLOCKING
    {
        return m_channelCount;
    }

    [[nodiscard]] std::uint32_t frames() const noexcept RB_NONBLOCKING
    {
        return m_frames;
    }

    /// Fills every channel with zeroes.
    void clear() noexcept RB_NONBLOCKING
    {
        for (float &sample : m_samples) {
            sample = 0.0f;
        }
    }

    [[nodiscard]] std::span<float> channel(std::uint32_t channel) noexcept RB_NONBLOCKING
    {
        if (channel >= m_channelCount) {
            return {};
        }
        return {m_channels[channel], m_frames};
    }

    [[nodiscard]] std::span<const float> channel(std::uint32_t channel) const noexcept RB_NONBLOCKING
    {
        if (channel >= m_channelCount) {
            return {};
        }
        return {m_channels[channel], m_frames};
    }

    /// A view of the whole buffer.
    [[nodiscard]] AudioBlock block() noexcept RB_NONBLOCKING
    {
        return {m_channels.data(), m_channelCount, m_frames};
    }

    /// A view of the first \a frames frames, for testing a shorter cycle.
    [[nodiscard]] AudioBlock block(std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        return block().withFrames(frames);
    }

private:
    std::vector<float> m_samples;
    std::vector<float *> m_channels;
    std::uint32_t m_channelCount = 0;
    std::uint32_t m_frames = 0;
};

} // namespace rb::dsp

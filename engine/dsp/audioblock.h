// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"

#include <algorithm>
#include <cstdint>
#include <span>

namespace rb::dsp
{

/// The shortest cycle a block must handle.
inline constexpr std::uint32_t kMinBlockFrames = 1;

/// The longest cycle a block must handle. Buffers are prepared for this size,
/// so a change of PipeWire's quantum never needs new memory.
inline constexpr std::uint32_t kMaxBlockFrames = 8192;

/// A non-owning view of planar audio: one span of samples per channel, all of
/// the same length. This is how audio is handed to a block for one cycle.
///
/// The channel pointers belong to the caller, and a null one means the channel
/// is silent: it has no storage and must not be written. That is how an
/// unconnected input port arrives, so every reader has to ask isSilent() (or
/// use the empty span that channel() returns) rather than assume storage.
///
/// Nothing here allocates: constructing, copying and reading a view are all
/// safe on the audio thread.
class AudioBlock
{
public:
    AudioBlock() = default;

    /// Wraps \a channelCount pointers, each addressing \a frames samples.
    /// \a channels may itself be null when \a channelCount is zero.
    AudioBlock(float *const *channels, std::uint32_t channelCount, std::uint32_t frames) noexcept RB_NONBLOCKING
        : m_channels(channels),
          m_channelCount(channelCount),
          m_frames(frames)
    {
    }

    [[nodiscard]] std::uint32_t channelCount() const noexcept RB_NONBLOCKING
    {
        return m_channelCount;
    }

    /// The number of frames in every channel of this cycle.
    [[nodiscard]] std::uint32_t frames() const noexcept RB_NONBLOCKING
    {
        return m_frames;
    }

    [[nodiscard]] bool isEmpty() const noexcept RB_NONBLOCKING
    {
        return m_channelCount == 0 || m_frames == 0;
    }

    /// Whether \a channel carries no samples, because it has no storage or is
    /// out of range. Reading such a channel means reading silence.
    [[nodiscard]] bool isSilent(std::uint32_t channel) const noexcept RB_NONBLOCKING
    {
        return channel >= m_channelCount || m_channels[channel] == nullptr;
    }

    /// The samples of \a channel, or an empty span when it is silent. The span
    /// is writable; writing to a channel of an input block's view is not.
    [[nodiscard]] std::span<float> channel(std::uint32_t channel) const noexcept RB_NONBLOCKING
    {
        if (isSilent(channel)) {
            return {};
        }
        return {m_channels[channel], m_frames};
    }

    /// The same cycle, seen as a slice of its channels: \a count of them
    /// starting at \a from, or as many as there are.
    ///
    /// **How a block that takes audio from two places at once tells one
    /// from the other.** An Application source that is also capturing the
    /// microphone the application is recording from is given both groups on
    /// one set of ports, because a port carries one channel and the two
    /// groups have channels of the same name; PrepareSpec::secondInputFrom
    /// says where the second one starts.
    [[nodiscard]] AudioBlock channels(std::uint32_t from, std::uint32_t count) const noexcept RB_NONBLOCKING
    {
        if (from >= m_channelCount) {
            return {};
        }
        return {m_channels + from, std::min(count, m_channelCount - from), m_frames};
    }

    /// The same channels, seen as a shorter cycle. Used to run a graph in
    /// steps, for example up to and then past a parameter change.
    [[nodiscard]] AudioBlock withFrames(std::uint32_t frames) const noexcept RB_NONBLOCKING
    {
        return {m_channels, m_channelCount, frames < m_frames ? frames : m_frames};
    }

private:
    float *const *m_channels = nullptr;
    std::uint32_t m_channelCount = 0;
    std::uint32_t m_frames = 0;
};

} // namespace rb::dsp

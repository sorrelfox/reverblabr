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

/// Holds every channel of a signal back by a whole number of samples.
///
/// This is the lookahead line a dynamics processor puts in the signal path so
/// that its detector, which reads the signal early, has already decided what
/// to do by the time the samples arrive. It is also the delay that latency
/// compensation uses to line up two paths of different lengths.
///
/// The maximum delay is fixed by prepare(), which is the only thing that
/// allocates. Delaying by more than that is not possible, so a block asks for
/// the longest delay it will ever need and then moves within it.
class DelayLine
{
public:
    /// Allocates room for \a channelCount channels delayed by up to
    /// \a maxDelaySamples, sets the delay to the maximum and clears the line.
    /// Not real-time safe; call it from prepare().
    void prepare(std::uint32_t channelCount, std::uint32_t maxDelaySamples)
    {
        m_channelCount = channelCount;
        m_maxDelay = maxDelaySamples;
        // One more slot than the delay, so writing this sample never
        // overwrites the one that is about to be read.
        m_capacity = maxDelaySamples + 1;
        m_samples.assign(static_cast<std::size_t>(channelCount) * m_capacity, 0.0f);
        m_delay = maxDelaySamples;
        m_writeIndex = 0;
    }

    [[nodiscard]] std::uint32_t channelCount() const noexcept RB_NONBLOCKING
    {
        return m_channelCount;
    }

    [[nodiscard]] std::uint32_t maxDelaySamples() const noexcept RB_NONBLOCKING
    {
        return m_maxDelay;
    }

    /// The delay in effect, in samples.
    [[nodiscard]] std::uint32_t delaySamples() const noexcept RB_NONBLOCKING
    {
        return m_delay;
    }

    /// Sets the delay, clamped to the maximum prepare() allowed.
    ///
    /// The change takes effect at the next sample, so the signal jumps; only
    /// change it while the line is silent, or fade around it. A lookahead
    /// delay is set once, in prepare().
    void setDelaySamples(std::uint32_t samples) noexcept RB_NONBLOCKING
    {
        m_delay = samples < m_maxDelay ? samples : m_maxDelay;
    }

    /// Fills the line with silence, so nothing from before is heard again.
    void reset() noexcept RB_NONBLOCKING
    {
        for (float &sample : m_samples) {
            sample = 0.0f;
        }
        m_writeIndex = 0;
    }

    /// Delays \a block in place: every channel comes out delaySamples()
    /// samples later than it went in. Channels of \a block beyond
    /// channelCount() are left alone. A silent channel has no storage to
    /// delay, so its line is filled with silence instead, which keeps what it
    /// carried before the channel fell silent from coming back later.
    void process(AudioBlock block) noexcept RB_NONBLOCKING
    {
        if (m_capacity == 0) {
            return;
        }
        const std::uint32_t channels = block.channelCount() < m_channelCount ? block.channelCount() : m_channelCount;
        const std::uint32_t frames = block.frames();
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> samples = block.channel(channel);
            if (samples.empty()) {
                pushSilence(channel, frames);
            } else {
                processChannel(channel, samples);
            }
        }
        m_writeIndex = advanced(m_writeIndex, frames);
    }

    /// Delays one channel in place, for a caller that holds its channels
    /// separately. Advance the line with endFrames() once every channel of
    /// the cycle has been through.
    void processChannel(std::uint32_t channel, std::span<float> samples) noexcept RB_NONBLOCKING
    {
        if (channel >= m_channelCount || m_capacity == 0) {
            return;
        }
        float *const line = m_samples.data() + static_cast<std::size_t>(channel) * m_capacity;
        std::uint32_t write = m_writeIndex;
        // Written before it is read, so a delay of zero passes the sample
        // straight through instead of reading the slot's previous contents.
        std::uint32_t read = advanced(write, m_capacity - m_delay);
        for (float &sample : samples) {
            line[write] = sample;
            sample = line[read];
            if (++write == m_capacity) {
                write = 0;
            }
            if (++read == m_capacity) {
                read = 0;
            }
        }
    }

    /// Delays one channel in place through **two read heads at once**, and
    /// lets \a mix make each output sample from what the two read: `mix(a,
    /// b)`, where \a a is the sample from \a delayA ago and \a b the one from
    /// \a delayB ago, called once a sample in order.
    ///
    /// This is how a delay is moved without a click: the old delay and
    /// the new one are both read for as long as the fade between them lasts,
    /// and neither is ever jumped. Both delays are clamped to the maximum
    /// prepare() allowed; delaySamples() is not used or changed. Advance the
    /// line with endFrames() once every channel of the cycle has been
    /// through, as with processChannel().
    template <typename Mix>
    void processChannelTwoTaps(std::uint32_t channel,
                               std::span<float> samples,
                               std::uint32_t delayA,
                               std::uint32_t delayB,
                               Mix &&mix) noexcept RB_NONBLOCKING
    {
        if (channel >= m_channelCount || m_capacity == 0) {
            return;
        }
        delayA = delayA < m_maxDelay ? delayA : m_maxDelay;
        delayB = delayB < m_maxDelay ? delayB : m_maxDelay;
        float *const line = m_samples.data() + static_cast<std::size_t>(channel) * m_capacity;
        std::uint32_t write = m_writeIndex;
        // Written before either is read, as in processChannel(), so a delay
        // of zero is the sample itself.
        std::uint32_t readA = advanced(write, m_capacity - delayA);
        std::uint32_t readB = advanced(write, m_capacity - delayB);
        for (float &sample : samples) {
            line[write] = sample;
            sample = mix(line[readA], line[readB]);
            if (++write == m_capacity) {
                write = 0;
            }
            if (++readA == m_capacity) {
                readA = 0;
            }
            if (++readB == m_capacity) {
                readB = 0;
            }
        }
    }

    /// Feeds \a frames samples of silence into one channel of the line,
    /// without producing its output. Used for a channel that has no storage.
    void pushSilence(std::uint32_t channel, std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        if (channel >= m_channelCount || m_capacity == 0) {
            return;
        }
        float *const line = m_samples.data() + static_cast<std::size_t>(channel) * m_capacity;
        std::uint32_t write = m_writeIndex;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            line[write] = 0.0f;
            if (++write == m_capacity) {
                write = 0;
            }
        }
    }

    /// Moves the line on by \a frames, after processChannel() has been used
    /// for every channel of a cycle.
    void endFrames(std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        m_writeIndex = advanced(m_writeIndex, frames);
    }

    /// Takes over what \a other holds — its samples and where it is in
    /// them — when the two lines are the same shape and the same delay, and
    /// answers whether it did.
    ///
    /// **What keeps a recompile from being heard as a gap.** A new graph
    /// makes new lines, and a new line is silence for as long as its delay;
    /// the samples an old one was holding would be lost at the handover. A
    /// line of a different shape or delay is lining up something different
    /// and starts empty. Real-time: a copy, no allocation.
    bool takeStateFrom(const DelayLine &other) noexcept RB_NONBLOCKING
    {
        if (other.m_channelCount != m_channelCount || other.m_capacity != m_capacity || other.m_delay != m_delay
            || other.m_samples.size() != m_samples.size()) {
            return false;
        }
        for (std::size_t index = 0; index < m_samples.size(); ++index) {
            m_samples[index] = other.m_samples[index];
        }
        m_writeIndex = other.m_writeIndex;
        return true;
    }

private:
    [[nodiscard]] std::uint32_t advanced(std::uint32_t index, std::uint32_t by) const noexcept RB_NONBLOCKING
    {
        if (m_capacity == 0) {
            return 0;
        }
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(index) + by) % m_capacity);
    }

    std::vector<float> m_samples;
    std::uint32_t m_channelCount = 0;
    std::uint32_t m_capacity = 0;
    std::uint32_t m_maxDelay = 0;
    std::uint32_t m_delay = 0;
    std::uint32_t m_writeIndex = 0;
};

} // namespace rb::dsp

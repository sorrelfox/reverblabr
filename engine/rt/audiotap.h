// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"
#include "rt/spscqueue.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rb::rt
{

/// Something the audio thread has to say about the frames it is writing,
/// pinned to the frame it applies from.
struct TapMarker {
    enum class Kind : std::uint8_t {
        /// The frames from here on are at \a value frames a second. The
        /// first thing a tap ever carries, and again whenever PipeWire moves
        /// the graph to another rate, which a file cannot follow.
        Rate,
    };

    /// Counted from the first frame the tap was ever given, so a marker
    /// means the same thing however much has been read in the meantime.
    std::uint64_t atFrame = 0;
    Kind kind = Kind::Rate;
    std::uint32_t value = 0;
};

/// Audio on its way from the audio thread to a thread that writes it to
/// disk: a ring of interleaved frames between exactly two threads.
///
/// **The audio thread never waits for the disk.** A cycle copies its frames
/// in and carries on; the writer takes them out when it gets round to it,
/// which may be tens of milliseconds later and, while a disk is busy,
/// seconds. The ring is made long enough for that (the Recorder asks for
/// ten seconds and more), so a busy disk costs latency on the way to the
/// file and nothing else.
///
/// **A ring that is full drops the cycle, whole, and counts it.** Waiting is
/// not an option on the audio thread, and writing part of a cycle would
/// leave a click in the middle of the file as well as a gap. The writer
/// reads droppedFrames() and says how much was lost, which is how a person
/// finds out the disk could not keep up rather than finding a recording
/// that is quietly shorter than the session.
///
/// Positions are 64-bit frame counts from the first frame ever written, so
/// they never wrap in any recording anybody will make: at 384 kHz a 64-bit
/// count lasts a million years.
class AudioTap
{
public:
    /// The most channels a tap carries, which is what the audio thread's
    /// pointer array is sized for.
    static constexpr std::uint32_t kMaxChannels = 8;

    /// Builds a ring for \a capacityFrames frames of \a channels channels.
    /// This allocates, so it belongs off the audio thread.
    AudioTap(std::uint32_t channels, std::size_t capacityFrames, std::size_t markerCapacity = 64)
        : m_channels(std::clamp<std::uint32_t>(channels, 1, kMaxChannels))
        , m_capacity(std::max<std::size_t>(capacityFrames, 1))
        , m_samples(m_capacity * m_channels, 0.0f)
        , m_markers(markerCapacity)
    {
    }

    AudioTap(const AudioTap &) = delete;
    AudioTap &operator=(const AudioTap &) = delete;

    [[nodiscard]] std::uint32_t channelCount() const noexcept RB_NONBLOCKING
    {
        return m_channels;
    }

    [[nodiscard]] std::size_t capacityFrames() const noexcept RB_NONBLOCKING
    {
        return m_capacity;
    }

    // ------------------------------------------------------------ producer

    /// Says that the frames from the next one written on are at
    /// \a sampleRate. **Only when it changes**: a block that is replaced by
    /// a fresh one at the same rate says the same thing again, and the file
    /// must not be split for it. Audio thread.
    void announceRate(std::uint32_t sampleRate) noexcept RB_NONBLOCKING
    {
        if (sampleRate == m_announcedRate) {
            return;
        }
        TapMarker marker;
        marker.atFrame = m_written.load(std::memory_order_relaxed);
        marker.kind = TapMarker::Kind::Rate;
        marker.value = sampleRate;
        if (m_markers.tryPush(marker)) {
            m_announcedRate = sampleRate;
        } else {
            // Said again on the next cycle, at a later frame: the rate is
            // then wrong for a cycle or so rather than for the whole file.
            m_markersLost.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /// Adds one cycle: \a frames of each of \a channels, where a null
    /// pointer is a channel of silence and a channel beyond the ones given
    /// is silence too. **All or nothing**: a cycle that does not fit is
    /// dropped whole and counted. Audio thread.
    bool write(std::span<const float *const> channels, std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        if (frames == 0) {
            return true;
        }
        const std::uint64_t written = m_written.load(std::memory_order_relaxed);
        const std::uint64_t read = m_read.load(std::memory_order_acquire);
        if (written - read + frames > m_capacity) {
            m_dropped.fetch_add(frames, std::memory_order_relaxed);
            return false;
        }
        std::size_t slot = static_cast<std::size_t>(written % m_capacity);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            float *out = &m_samples[slot * m_channels];
            for (std::uint32_t channel = 0; channel < m_channels; ++channel) {
                const float *in = channel < channels.size() ? channels[channel] : nullptr;
                out[channel] = in != nullptr ? in[frame] : 0.0f;
            }
            slot = slot + 1 == m_capacity ? 0 : slot + 1;
        }
        // Publishes the frames: a reader that sees the new count also sees
        // the samples written above it.
        m_written.store(written + frames, std::memory_order_release);
        return true;
    }

    // ------------------------------------------------------------ consumer

    /// How many frames the producer has ever written, as far as the reader
    /// can see.
    [[nodiscard]] std::uint64_t framesWritten() const noexcept RB_NONBLOCKING
    {
        return m_written.load(std::memory_order_acquire);
    }

    /// How many frames the reader has taken or skipped.
    [[nodiscard]] std::uint64_t framesRead() const noexcept RB_NONBLOCKING
    {
        return m_read.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t readableFrames() const noexcept RB_NONBLOCKING
    {
        return static_cast<std::size_t>(framesWritten() - framesRead());
    }

    /// Copies up to \a maxFrames interleaved frames into \a out, which holds
    /// at least that many times channelCount() samples, and gives back how
    /// many it copied. Consumer only.
    std::size_t read(std::span<float> out, std::size_t maxFrames) noexcept RB_NONBLOCKING
    {
        const std::size_t frames =
            std::min({maxFrames, readableFrames(), out.size() / static_cast<std::size_t>(m_channels)});
        const std::uint64_t read = m_read.load(std::memory_order_relaxed);
        std::size_t slot = static_cast<std::size_t>(read % m_capacity);
        std::size_t done = 0;
        while (done < frames) {
            // In at most two runs: to the end of the storage, then from its
            // start.
            const std::size_t run = std::min(frames - done, m_capacity - slot);
            std::copy_n(&m_samples[slot * m_channels], run * m_channels, &out[done * m_channels]);
            done += run;
            slot = 0;
        }
        // Gives the slots back: the producer that sees the new count may
        // write over them, and the copy above is finished by then.
        m_read.store(read + frames, std::memory_order_release);
        return frames;
    }

    /// Lets go of up to \a maxFrames frames without looking at them, for a
    /// writer that is not recording. Consumer only.
    std::size_t discard(std::size_t maxFrames) noexcept RB_NONBLOCKING
    {
        const std::size_t frames = std::min(maxFrames, readableFrames());
        m_read.store(framesRead() + frames, std::memory_order_release);
        return frames;
    }

    /// The oldest marker, if there is one. Consumer only.
    [[nodiscard]] bool popMarker(TapMarker &marker) noexcept RB_NONBLOCKING
    {
        return m_markers.tryPop(marker);
    }

    /// Frames dropped because the ring was full, since the tap was made.
    [[nodiscard]] std::uint64_t droppedFrames() const noexcept RB_NONBLOCKING
    {
        return m_dropped.load(std::memory_order_relaxed);
    }

    /// Markers that did not fit and had to be said again later.
    [[nodiscard]] std::uint64_t markersLost() const noexcept RB_NONBLOCKING
    {
        return m_markersLost.load(std::memory_order_relaxed);
    }

    // ------------------------------------------------------------ gaps

    /// **Which of the node's input lanes a gap is read from**: the
    /// lanes of the Application sources feeding the recording that leave a
    /// pause in their application's audio out of the file. A cycle in
    /// which none of them had audio arriving is not written at all, so the
    /// two parts either side of the pause join. Zero, the default, writes
    /// every cycle.
    ///
    /// Set by whoever plans the session, whenever the lanes are numbered
    /// again; read by the producer every cycle.
    void setGapLanes(std::uint32_t lanes) noexcept
    {
        m_gapLanes.store(lanes, std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t gapLanes() const noexcept RB_NONBLOCKING
    {
        return m_gapLanes.load(std::memory_order_relaxed);
    }

private:
    const std::uint32_t m_channels;
    const std::size_t m_capacity;
    std::vector<float> m_samples;
    SpscQueue<TapMarker> m_markers;
    /// Producer only: the last rate a marker carried.
    std::uint32_t m_announcedRate = 0;
    alignas(kCacheLineBytes) std::atomic<std::uint64_t> m_written{0};
    alignas(kCacheLineBytes) std::atomic<std::uint64_t> m_read{0};
    alignas(kCacheLineBytes) std::atomic<std::uint64_t> m_dropped{0};
    std::atomic<std::uint64_t> m_markersLost{0};
    std::atomic<std::uint32_t> m_gapLanes{0};
};

} // namespace rb::rt

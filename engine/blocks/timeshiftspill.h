// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/audiotap.h"
#include "rt/realtime.h"
#include "rt/spscqueue.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace rb::blocks
{

/// Frames a TimeShiftSpill's tap had no room for: \a frames of them, where
/// the tap had written \a atTapFrame.
struct TimeShiftGap {
    std::uint64_t atTapFrame = 0;
    std::uint64_t frames = 0;
};

/// The part of a Time Shift that touches the disk, on a thread of its own.
///
/// **Two jobs.** It takes what the audio thread keeps (through an
/// rt::AudioTap) and writes it into a file used as a ring: position p of
/// the stream is frame p modulo the file's length. And, asked for a
/// position, it reads the file from there into a ring of its own that the
/// audio thread reads from, keeping ahead of the play head.
///
/// **The file is anonymous**: unlinked as soon as it is opened, so it goes
/// with the block, even after a crash, and there is nothing to clean up.
///
/// **Positions stay true when the disk falls behind.** A cycle the tap has
/// no room for is dropped whole, and the audio thread says where and how
/// long (a queue of gaps): the file gets silence there, not the next cycle
/// early, so everything after it is still where the audio thread thinks.
///
/// **Handing over the far ring.** The audio thread asks with a generation
/// count and a start; the spill resets the ring to that start, publishes
/// the same generation, then fills it. The audio thread reads a position
/// only while the generation it asked for is the one published and only
/// from where it last said it had read to; the spill writes a slot only
/// once the audio thread has said it has read past it. So no slot is
/// written by one thread while the other reads it.
class TimeShiftSpill
{
public:
    static constexpr std::uint32_t kChannels = 2;

    /// How long the thread sleeps once it has caught up. **Long, because
    /// the rings are**: seconds of headroom each way (TimeShift's tap and
    /// far ring hold 8 s, a static_assert keeps them at least a hundred
    /// polls deep), so waking 50 times a second costs a battery nothing
    /// and loses nothing. The audio thread never wakes it: a jump into the
    /// file waits at most one poll to be served, well inside the fade a
    /// person hears anyway.
    static constexpr std::chrono::milliseconds kPoll{20};

    enum class Mode : std::uint8_t {
        /// A thread of its own does the work: all there is, then it
        /// sleeps for kPoll, or until the spill is destroyed.
        Threaded,
        /// Nothing happens until step() is called: for a test that needs
        /// to say exactly when the disk catches up.
        Stepped,
    };

    /// Opens an anonymous file in \a directory that holds \a fileFrames
    /// frames, a tap \a tapFrames long and a far ring \a farFrames long.
    TimeShiftSpill(const std::filesystem::path &directory,
                   std::uint64_t fileFrames,
                   std::size_t tapFrames,
                   std::size_t farFrames,
                   Mode mode = Mode::Threaded);
    ~TimeShiftSpill();

    TimeShiftSpill(const TimeShiftSpill &) = delete;
    TimeShiftSpill &operator=(const TimeShiftSpill &) = delete;

    /// Whether the file could be made; a spill without one does nothing.
    [[nodiscard]] bool ok() const noexcept RB_NONBLOCKING
    {
        return m_file >= 0;
    }

    [[nodiscard]] std::uint64_t fileFrames() const noexcept RB_NONBLOCKING
    {
        return m_fileFrames;
    }

    // ---------------------------------------------------------- audio side

    /// Keeps one cycle. The audio thread's own positions and the file's
    /// stay the same even when it cannot be kept.
    void write(std::span<const float *const> channels, std::uint32_t frames) noexcept RB_NONBLOCKING;

    /// Asks for the file to be read into the far ring from \a start on.
    void request(std::uint64_t start) noexcept RB_NONBLOCKING;

    /// Whether the far ring can be read at \a position now, for the last
    /// request.
    [[nodiscard]] bool has(std::uint64_t position) const noexcept RB_NONBLOCKING;

    /// Whether \a position will be in the far ring for the last request,
    /// once the spill has read that far.
    [[nodiscard]] bool covers(std::uint64_t position) const noexcept RB_NONBLOCKING;

    [[nodiscard]] float sample(std::uint64_t position, std::uint32_t channel) const noexcept RB_NONBLOCKING
    {
        return m_far[static_cast<std::size_t>(position % m_farFrames) * kChannels + channel];
    }

    /// Says nothing before \a position is wanted from the far ring any
    /// more, which frees it for the spill to fill. Never goes back.
    void consumed(std::uint64_t position) noexcept RB_NONBLOCKING;

    // -------------------------------------------------------- either side

    /// How many frames of the stream have reached the file.
    [[nodiscard]] std::uint64_t flushed() const noexcept RB_NONBLOCKING
    {
        return m_flushed.load(std::memory_order_acquire);
    }

    /// Frames the tap had no room for, which the file has as silence.
    [[nodiscard]] std::uint64_t dropped() const noexcept RB_NONBLOCKING
    {
        return m_tap.droppedFrames();
    }

    // ---------------------------------------------------------- disk side

    /// Does one round of the work: writes what the tap holds, and reads the
    /// file into the far ring. False if there was nothing to do. Only for
    /// a spill made Stepped; a threaded one calls it itself.
    bool step();

    /// How many times the thread has woken, for a test that holds it to
    /// kPoll.
    [[nodiscard]] std::uint64_t wakeups() const noexcept
    {
        return m_wakeups.load(std::memory_order_relaxed);
    }

    /// How many spill threads are running in the process now, counted by
    /// the threads themselves as they start and finish: for a test that
    /// holds a block to starting one only when it spills, and to taking it
    /// with it. Not the process's thread count, which a sanitizer's or a
    /// runtime's own threads change under it.
    [[nodiscard]] static int threadsRunning() noexcept
    {
        return s_running.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t kChunkFrames = 8192;

    void run();
    bool drain();
    bool fill();
    void writeFile(const float *frames, std::size_t count, std::uint64_t position);

    std::uint64_t m_fileFrames;
    rt::AudioTap m_tap;
    rt::SpscQueue<TimeShiftGap> m_gaps;
    std::vector<float> m_far;
    std::uint64_t m_farFrames;
    std::vector<float> m_chunk;
    std::vector<float> m_silence;
    int m_file = -1;
    std::thread m_thread;
    /// The destructor's word to the thread, which it sleeps on.
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stop = false;
    std::atomic<std::uint64_t> m_wakeups{0};
    static inline std::atomic<int> s_running{0};

    std::atomic<std::uint64_t> m_flushed{0};
    std::atomic<std::uint64_t> m_requestStart{0};
    std::atomic<std::uint32_t> m_requestGeneration{0};
    std::atomic<std::uint32_t> m_readyGeneration{0};
    std::atomic<std::uint64_t> m_farStart{0};
    std::atomic<std::uint64_t> m_farFilled{0};
    std::atomic<std::uint64_t> m_farConsumed{0};
    std::atomic<std::uint32_t> m_consumedGeneration{0};

    /// The audio thread's own: the last generation it asked for, where it
    /// has said it read to, and frames lost that it has not yet queued.
    std::uint32_t m_asked = 0;
    std::uint64_t m_audioConsumed = 0;
    std::uint64_t m_gapFrames = 0;

    /// The disk side's own: the generation it serves, how far it has read
    /// the tap, and the gap it is working towards.
    std::uint32_t m_served = 0;
    std::uint64_t m_tapRead = 0;
    TimeShiftGap m_gap;
    bool m_haveGap = false;
};

} // namespace rb::blocks

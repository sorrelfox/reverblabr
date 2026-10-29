// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/timeshiftspill.h"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <random>
#include <string>
#include <system_error>

namespace rb::blocks
{

namespace
{

/// Enough for every gap a tap of seconds could see before the disk side
/// looks again: one per cycle it could not keep.
constexpr std::size_t kGapQueue = 256;

} // namespace

TimeShiftSpill::TimeShiftSpill(const std::filesystem::path &directory,
                               std::uint64_t fileFrames,
                               std::size_t tapFrames,
                               std::size_t farFrames,
                               Mode mode)
    : m_fileFrames(std::max<std::uint64_t>(fileFrames, 1))
    , m_tap(kChannels, tapFrames)
    , m_gaps(kGapQueue)
    , m_far(std::max<std::size_t>(farFrames, 1) * kChannels, 0.0f)
    , m_farFrames(std::max<std::size_t>(farFrames, 1))
    , m_chunk(kChunkFrames * kChannels, 0.0f)
    , m_silence(kChunkFrames * kChannels, 0.0f)
{
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::random_device random;
    const std::filesystem::path path =
        directory / ("timeshift-" + std::to_string(::getpid()) + "-" + std::to_string(random()) + ".raw");
    m_file = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (m_file < 0) {
        return;
    }
    ::unlink(path.c_str());
    if (mode == Mode::Threaded) {
        m_thread = std::thread([this] { run(); });
    }
}

TimeShiftSpill::~TimeShiftSpill()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
    if (m_file >= 0) {
        ::close(m_file);
    }
}

void TimeShiftSpill::write(std::span<const float *const> channels, std::uint32_t frames) noexcept RB_NONBLOCKING
{
    // **A gap goes first**, where it happened: the frames after it are not
    // written until the disk side has been told about it.
    if (m_gapFrames > 0) {
        TimeShiftGap gap;
        gap.atTapFrame = m_tap.framesWritten();
        gap.frames = m_gapFrames;
        if (!m_gaps.tryPush(gap)) {
            m_gapFrames += frames;
            return;
        }
        m_gapFrames = 0;
    }
    if (!m_tap.write(channels, frames)) {
        m_gapFrames += frames;
    }
}

void TimeShiftSpill::request(std::uint64_t start) noexcept RB_NONBLOCKING
{
    m_requestStart.store(start, std::memory_order_relaxed);
    m_asked = m_requestGeneration.load(std::memory_order_relaxed) + 1;
    m_audioConsumed = start;
    m_requestGeneration.store(m_asked, std::memory_order_release);
}

bool TimeShiftSpill::has(std::uint64_t position) const noexcept RB_NONBLOCKING
{
    if (m_asked == 0 || m_readyGeneration.load(std::memory_order_acquire) != m_asked) {
        return false;
    }
    return position >= m_audioConsumed && position < m_farFilled.load(std::memory_order_acquire);
}

bool TimeShiftSpill::covers(std::uint64_t position) const noexcept RB_NONBLOCKING
{
    return m_asked != 0 && position >= m_audioConsumed && position < m_audioConsumed + m_farFrames;
}

void TimeShiftSpill::consumed(std::uint64_t position) noexcept RB_NONBLOCKING
{
    if (m_asked == 0 || position <= m_audioConsumed) {
        return;
    }
    m_audioConsumed = position;
    m_farConsumed.store(position, std::memory_order_relaxed);
    m_consumedGeneration.store(m_asked, std::memory_order_release);
}

bool TimeShiftSpill::step()
{
    if (m_file < 0) {
        return false;
    }
    bool busy = drain();
    const std::uint32_t asked = m_requestGeneration.load(std::memory_order_acquire);
    if (asked != m_served) {
        m_served = asked;
        const std::uint64_t start = m_requestStart.load(std::memory_order_relaxed);
        m_farStart.store(start, std::memory_order_relaxed);
        m_farFilled.store(start, std::memory_order_relaxed);
        m_readyGeneration.store(m_served, std::memory_order_release);
        busy = true;
    }
    if (m_served != 0 && fill()) {
        busy = true;
    }
    return busy;
}

void TimeShiftSpill::run()
{
    s_running.fetch_add(1, std::memory_order_acq_rel);
    std::unique_lock lock(m_mutex);
    while (!m_stop) {
        lock.unlock();
        // Everything there is to do, then a sleep: never a wake for one
        // cycle's worth.
        while (step()) { }
        lock.lock();
        m_wake.wait_for(lock, kPoll, [this] { return m_stop; });
        m_wakeups.fetch_add(1, std::memory_order_relaxed);
    }
    s_running.fetch_sub(1, std::memory_order_acq_rel);
}

void TimeShiftSpill::writeFile(const float *frames, std::size_t count, std::uint64_t position)
{
    std::size_t done = 0;
    while (done < count) {
        const std::uint64_t slot = (position + done) % m_fileFrames;
        const auto run = static_cast<std::size_t>(std::min<std::uint64_t>(count - done, m_fileFrames - slot));
        (void)::pwrite(m_file,
                       frames + done * kChannels,
                       run * kChannels * sizeof(float),
                       static_cast<off_t>(slot * kChannels * sizeof(float)));
        done += run;
    }
}

bool TimeShiftSpill::drain()
{
    if (!m_haveGap) {
        m_haveGap = m_gaps.tryPop(m_gap);
    }
    std::uint64_t position = m_flushed.load(std::memory_order_relaxed);
    std::size_t limit = kChunkFrames;
    if (m_haveGap) {
        if (m_gap.atTapFrame == m_tapRead) {
            // Silence where the lost frames were: more than the file holds
            // is the whole file, once.
            if (m_gap.frames > m_fileFrames) {
                position += m_gap.frames - m_fileFrames;
                m_gap.frames = m_fileFrames;
            }
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(m_gap.frames, kChunkFrames));
            writeFile(m_silence.data(), count, position);
            m_gap.frames -= count;
            m_haveGap = m_gap.frames > 0;
            m_flushed.store(position + count, std::memory_order_release);
            return true;
        }
        limit = static_cast<std::size_t>(std::min<std::uint64_t>(limit, m_gap.atTapFrame - m_tapRead));
    }
    const std::size_t frames = m_tap.read(m_chunk, limit);
    if (frames == 0) {
        return false;
    }
    writeFile(m_chunk.data(), frames, position);
    m_tapRead += frames;
    m_flushed.store(position + frames, std::memory_order_release);
    return true;
}

bool TimeShiftSpill::fill()
{
    const std::uint64_t filled = m_farFilled.load(std::memory_order_relaxed);
    std::uint64_t consumed = m_farStart.load(std::memory_order_relaxed);
    if (m_consumedGeneration.load(std::memory_order_acquire) == m_served) {
        consumed = std::max(consumed, m_farConsumed.load(std::memory_order_relaxed));
    }
    const std::uint64_t ahead = filled > consumed ? filled - consumed : 0;
    const std::uint64_t room = m_farFrames - std::min(m_farFrames, ahead);
    const std::uint64_t flushed = m_flushed.load(std::memory_order_relaxed);
    const std::uint64_t there = flushed > filled ? flushed - filled : 0;
    const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>({room, there, kChunkFrames}));
    if (frames == 0) {
        return false;
    }
    std::size_t done = 0;
    while (done < frames) {
        const std::uint64_t position = filled + done;
        const std::uint64_t slot = position % m_fileFrames;
        const std::uint64_t ringSlot = position % m_farFrames;
        const auto run = static_cast<std::size_t>(
            std::min<std::uint64_t>({frames - done, m_fileFrames - slot, m_farFrames - ringSlot}));
        (void)::pread(m_file,
                      &m_far[static_cast<std::size_t>(ringSlot) * kChannels],
                      run * kChannels * sizeof(float),
                      static_cast<off_t>(slot * kChannels * sizeof(float)));
        done += run;
    }
    m_farFilled.store(filled + frames, std::memory_order_release);
    return true;
}

} // namespace rb::blocks

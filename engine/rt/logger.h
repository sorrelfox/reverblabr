// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/realtime.h"
#include "rt/spscqueue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace rb::rt
{

enum class LogLevel : std::uint8_t {
    Debug,
    Info,
    Warning,
    Error,
};

/// The name of a level, for a drained line: "debug", "info", "warning",
/// "error".
[[nodiscard]] const char *logLevelName(LogLevel level) noexcept;

/// One line of log as the audio thread leaves it: small, trivially copyable,
/// and holding nothing that has to be freed.
struct LogRecord {
    /// Counted up from one, so a gap in the numbers shows where records were
    /// dropped even in a log that was drained in pieces.
    std::uint64_t sequence = 0;
    /// A string literal, or anything else that outlives the drain. The audio
    /// thread copies the pointer, never the characters.
    const char *message = nullptr;
    std::int64_t first = 0;
    std::int64_t second = 0;
    std::uint8_t argumentCount = 0;
    LogLevel level = LogLevel::Info;
};

/// A log the audio thread can write to.
///
/// Writing a line normally means formatting, a lock and a write to a file
/// descriptor, none of which may happen in a process callback. Here the audio
/// thread copies a fixed-size record into a ring and returns; a normal thread
/// empties the ring and does the formatting and the writing.
///
/// Two things the audio thread does not do, and why:
/// - **It does not read the clock.** Reading it is a call out of our code
///   that a real-time checker has to take on trust. The record carries a
///   sequence number instead, and the drainer timestamps what it prints; with
///   a drain every frame the difference is not worth the risk.
/// - **It does not format.** The message is a pointer to a literal and the
///   numbers travel as numbers, so nothing is parsed or copied until later.
///
/// A full ring drops the record and counts it, because the alternative is
/// waiting. droppedCount() belongs in the diagnostics.
///
/// **One writing thread per logger.** Give each real-time thread its own.
class RealtimeLogger
{
public:
    /// Builds a log that holds \a capacity records between drains.
    explicit RealtimeLogger(std::size_t capacity)
        : m_records(capacity)
    {
    }

    RealtimeLogger(const RealtimeLogger &) = delete;
    RealtimeLogger &operator=(const RealtimeLogger &) = delete;

    /// Writes a line. \a message must outlive the next drain; a string
    /// literal always does. Safe on the audio thread.
    void log(LogLevel level, const char *message) noexcept RB_NONBLOCKING
    {
        push(level, message, 0, 0, 0);
    }

    void log(LogLevel level, const char *message, std::int64_t first) noexcept RB_NONBLOCKING
    {
        push(level, message, first, 0, 1);
    }

    void log(LogLevel level, const char *message, std::int64_t first, std::int64_t second) noexcept RB_NONBLOCKING
    {
        push(level, message, first, second, 2);
    }

    /// Hands every waiting record to \a sink, oldest first, and returns how
    /// many there were. Call it from a normal thread: \a sink is where the
    /// formatting and the writing happen.
    template <typename Sink>
    std::size_t drain(Sink &&sink)
    {
        std::size_t drained = 0;
        LogRecord record;
        while (m_records.tryPop(record)) {
            sink(record);
            ++drained;
        }
        return drained;
    }

    /// Writes every waiting record to standard error, one line each, and
    /// returns how many there were.
    std::size_t drainToStandardError();

    /// How many records have been written since the log was built.
    [[nodiscard]] std::uint64_t recordCount() const noexcept RB_NONBLOCKING
    {
        return m_sequence.load(std::memory_order_relaxed);
    }

    /// How many records were lost because the ring was full when they were
    /// written.
    [[nodiscard]] std::uint64_t droppedCount() const noexcept RB_NONBLOCKING
    {
        return m_dropped.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t capacity() const noexcept RB_NONBLOCKING
    {
        return m_records.capacity();
    }

private:
    void push(LogLevel level,
              const char *message,
              std::int64_t first,
              std::int64_t second,
              std::uint8_t argumentCount) noexcept RB_NONBLOCKING
    {
        const std::uint64_t sequence = m_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
        const LogRecord record{sequence, message, first, second, argumentCount, level};
        if (!m_records.tryPush(record)) {
            m_dropped.fetch_add(1, std::memory_order_relaxed);
        }
    }

    SpscQueue<LogRecord> m_records;
    std::atomic<std::uint64_t> m_sequence{0};
    std::atomic<std::uint64_t> m_dropped{0};
};

} // namespace rb::rt

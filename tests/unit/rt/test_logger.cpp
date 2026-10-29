// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "rt/logger.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace rb::rt;

TEST(RealtimeLogger, KeepsWhatWasWrittenUntilItIsDrained)
{
    RealtimeLogger logger(8);
    logger.log(LogLevel::Warning, "quantum changed", 512);

    std::vector<LogRecord> drained;
    EXPECT_EQ(logger.drain([&](const LogRecord &record) { drained.push_back(record); }), 1u);
    ASSERT_EQ(drained.size(), 1u);
    EXPECT_EQ(drained[0].level, LogLevel::Warning);
    EXPECT_STREQ(drained[0].message, "quantum changed");
    EXPECT_EQ(drained[0].first, 512);
    EXPECT_EQ(drained[0].argumentCount, 1u);
    EXPECT_EQ(drained[0].sequence, 1u);
}

TEST(RealtimeLogger, CarriesNoneOneOrTwoNumbers)
{
    RealtimeLogger logger(8);
    logger.log(LogLevel::Info, "started");
    logger.log(LogLevel::Debug, "cycle", 64);
    logger.log(LogLevel::Error, "underrun", 3, 48000);

    std::vector<LogRecord> drained;
    EXPECT_EQ(logger.drain([&](const LogRecord &record) { drained.push_back(record); }), 3u);
    ASSERT_EQ(drained.size(), 3u);
    EXPECT_EQ(drained[0].argumentCount, 0u);
    EXPECT_EQ(drained[1].argumentCount, 1u);
    EXPECT_EQ(drained[1].first, 64);
    EXPECT_EQ(drained[2].argumentCount, 2u);
    EXPECT_EQ(drained[2].first, 3);
    EXPECT_EQ(drained[2].second, 48000);
}

TEST(RealtimeLogger, DrainsOldestFirst)
{
    RealtimeLogger logger(16);
    for (std::int64_t number = 1; number <= 10; ++number) {
        logger.log(LogLevel::Info, "line", number);
    }
    std::int64_t expected = 1;
    EXPECT_EQ(logger.drain([&](const LogRecord &record) {
        EXPECT_EQ(record.first, expected);
        EXPECT_EQ(record.sequence, static_cast<std::uint64_t>(expected));
        ++expected;
    }),
              10u);
}

TEST(RealtimeLogger, DropsRatherThanWaitsWhenTheRingIsFull)
{
    // The audio thread cannot wait for the drainer, so a full ring loses the
    // line and says so.
    RealtimeLogger logger(4);
    for (int line = 0; line < 10; ++line) {
        logger.log(LogLevel::Info, "line");
    }
    EXPECT_EQ(logger.recordCount(), 10u);
    EXPECT_EQ(logger.droppedCount(), 6u);

    std::size_t kept = 0;
    EXPECT_EQ(logger.drain([&](const LogRecord &) { ++kept; }), 4u);
    EXPECT_EQ(kept, 4u);
}

TEST(RealtimeLogger, TheSequenceNumbersShowWhereLinesWereLost)
{
    RealtimeLogger logger(2);
    logger.log(LogLevel::Info, "kept", 1);
    logger.log(LogLevel::Info, "kept", 2);
    logger.log(LogLevel::Info, "lost", 3);

    std::vector<std::uint64_t> sequences;
    static_cast<void>(logger.drain([&](const LogRecord &record) { sequences.push_back(record.sequence); }));
    ASSERT_EQ(sequences.size(), 2u);
    EXPECT_EQ(sequences[0], 1u);
    EXPECT_EQ(sequences[1], 2u);

    logger.log(LogLevel::Info, "after", 4);
    std::vector<std::uint64_t> afterwards;
    static_cast<void>(logger.drain([&](const LogRecord &record) { afterwards.push_back(record.sequence); }));
    ASSERT_EQ(afterwards.size(), 1u);
    EXPECT_EQ(afterwards[0], 4u) << "the gap at 3 is where the dropped line was";
}

TEST(RealtimeLogger, DrainingAnEmptyLogDoesNothing)
{
    RealtimeLogger logger(4);
    EXPECT_EQ(logger.drain([](const LogRecord &) { FAIL() << "there was nothing to drain"; }), 0u);
}

TEST(RealtimeLogger, LevelsHaveNames)
{
    EXPECT_STREQ(logLevelName(LogLevel::Debug), "debug");
    EXPECT_STREQ(logLevelName(LogLevel::Info), "info");
    EXPECT_STREQ(logLevelName(LogLevel::Warning), "warning");
    EXPECT_STREQ(logLevelName(LogLevel::Error), "error");
}

TEST(RealtimeLogger, WritesToStandardErrorWhenAskedTo)
{
    RealtimeLogger logger(4);
    logger.log(LogLevel::Error, "xrun", 2, 48000);
    testing::internal::CaptureStderr();
    EXPECT_EQ(logger.drainToStandardError(), 1u);
    const std::string written = testing::internal::GetCapturedStderr();
    EXPECT_NE(written.find("xrun"), std::string::npos);
    EXPECT_NE(written.find("error"), std::string::npos);
    EXPECT_NE(written.find("48000"), std::string::npos);
}

TEST(RealtimeLogger, ALoggingThreadAndADrainingThreadDoNotLoseALine)
{
    // Whatever gets through must arrive whole and in order; whatever does not
    // must be counted. Nothing may be invented or repeated.
    constexpr std::int64_t kLines = 1'000'000;
    RealtimeLogger logger(512);

    std::atomic<bool> logging{true};
    std::int64_t outOfOrder = 0;
    std::int64_t received = 0;
    std::uint64_t previous = 0;

    // Only ever run by one thread at a time: the drainer until it is joined,
    // then this one.
    const auto check = [&](const LogRecord &record) {
        if (record.sequence <= previous) {
            ++outOfOrder;
        }
        previous = record.sequence;
        ++received;
    };

    std::thread drainer([&] {
        while (logging.load(std::memory_order_relaxed)) {
            static_cast<void>(logger.drain(check));
        }
    });

    for (std::int64_t line = 1; line <= kLines; ++line) {
        logger.log(LogLevel::Debug, "cycle", line);
    }
    logging.store(false, std::memory_order_relaxed);
    drainer.join();
    static_cast<void>(logger.drain(check));

    EXPECT_EQ(outOfOrder, 0);
    EXPECT_EQ(logger.recordCount(), static_cast<std::uint64_t>(kLines));
    EXPECT_EQ(received + static_cast<std::int64_t>(logger.droppedCount()), kLines)
        << "every line was either drained or counted as dropped";
}

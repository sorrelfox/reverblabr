// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Time Shift: the play head exactly where a jump says, to the sample;
// live, paused and delayed switched between without a click; and half an
// hour kept, most of it read back from the disk.
//
// **The signal counts.** The left channel carries the number of the frame
// (exact in a float up to 2^24, which is 35 minutes at 8 kHz) and the right
// one carries 1, so what comes out says which frame the head read and how
// loud the fade had it: left / right is the frame.

#include "allocationtrap.h"
#include "analysis.h"
#include "blocks/timeshift.h"
#include "blocks/timeshiftspill.h"
#include "blocksizeinvariance.h"
#include "dsp/biquad.h"
#include "dsp/decibel.h"
#include "offlinerunner.h"
#include "rt/meter.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <thread>

using namespace rb;
using namespace rb::blocks;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

[[nodiscard]] MultiSignal stereo(const Signal &signal)
{
    return MultiSignal{signal, signal};
}

/// A directory of the test's own for spill files, gone when it is.
class SpillDirectory
{
public:
    SpillDirectory()
    {
        std::random_device random;
        m_path = std::filesystem::temp_directory_path() / ("rb-timeshift-test-" + std::to_string(random()));
        std::filesystem::create_directories(m_path);
    }
    ~SpillDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }
    SpillDirectory(const SpillDirectory &) = delete;
    SpillDirectory &operator=(const SpillDirectory &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return m_path;
    }

    /// How many files are in it.
    [[nodiscard]] std::size_t files() const
    {
        return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator(m_path), {}));
    }

private:
    std::filesystem::path m_path;
};

/// A Time Shift fed the counting signal, with its spill (if any) stepped by
/// the test: between two pieces of audio the disk always catches up, so
/// what is checked is where the head reads, not how fast a disk is.
class Shifter
{
public:
    Shifter(double sampleRate, std::int64_t minutes, const std::filesystem::path &spillDirectory)
        : m_sampleRate(sampleRate)
    {
        block.setSpillDirectory(spillDirectory);
        block.setSpillMode(TimeShiftSpill::Mode::Stepped);
        EXPECT_TRUE(block.setParam("minutes", ParamValue::ofInt(minutes)).has_value());
        m_runner = std::make_unique<BlockRunner>(block, 2, 2, sampleRate, 2048);
        m_runner->setMeter(&meter);
        EXPECT_TRUE(m_runner->prepare().has_value());
    }

    /// Feeds \a frames more of the counting signal, a piece at a time with
    /// the disk catching up in between, and gives back what came out.
    MultiSignal feed(std::size_t frames, const CycleShape &shape = CycleShape::fixed(512))
    {
        MultiSignal out{Signal{}, Signal{}};
        out[0].reserve(frames);
        out[1].reserve(frames);
        while (frames > 0) {
            const std::size_t piece = std::min<std::size_t>(frames, kPiece);
            MultiSignal input{Signal(piece), Signal(piece, 1.0f)};
            for (std::size_t frame = 0; frame < piece; ++frame) {
                input[0][frame] = static_cast<float>(m_fed + frame);
            }
            const MultiSignal got = m_runner->run(input, shape);
            out[0].insert(out[0].end(), got[0].begin(), got[0].end());
            out[1].insert(out[1].end(), got[1].begin(), got[1].end());
            m_fed += piece;
            frames -= piece;
            catchUp();
        }
        return out;
    }

    /// Feeds \a seconds and throws the output away.
    void skip(double seconds)
    {
        (void)feed(frames(seconds));
    }

    void catchUp()
    {
        if (TimeShiftSpill *spill = block.spill()) {
            while (spill->step()) { }
        }
    }

    /// Asks for the head to go \a seconds behind live, as the transport does.
    void jump(double seconds)
    {
        ++m_jumps;
        EXPECT_TRUE(block.setParam("target", ParamValue::ofFloat(static_cast<float>(seconds))).has_value());
        EXPECT_TRUE(block.setParam("jump", ParamValue::ofInt(m_jumps)).has_value());
    }

    /// Jumps, then feeds a cycle at a time, the disk catching up after each,
    /// until the jump has been made; gives back how many frames that took.
    std::size_t jumpAndWait(double seconds)
    {
        jump(seconds);
        std::size_t waited = 0;
        do {
            (void)feed(64, CycleShape::fixed(64));
            waited += 64;
        } while (block.jumpPending() && waited < 48000);
        EXPECT_FALSE(block.jumpPending()) << "the jump to " << seconds << " s never happened";
        return waited;
    }

    void pause(bool paused)
    {
        EXPECT_TRUE(block.setParam("paused", ParamValue::ofBool(paused)).has_value());
    }

    [[nodiscard]] std::size_t frames(double seconds) const
    {
        return static_cast<std::size_t>(std::llround(seconds * m_sampleRate));
    }

    [[nodiscard]] std::uint64_t fed() const noexcept
    {
        return m_fed;
    }

    TimeShift block;
    rt::BlockMeter meter;

private:
    static constexpr std::size_t kPiece = 32768;

    double m_sampleRate;
    std::unique_ptr<BlockRunner> m_runner;
    std::uint64_t m_fed = 0;
    std::int64_t m_jumps = 0;
};

/// How many frames of \a out, from \a from on, are not the frame that is
/// \a offset behind the one that went in at that moment; \a firstFed is the
/// number of the frame that went in with out[0]. Prints the first.
[[nodiscard]] std::size_t
wrongFrames(const MultiSignal &out, std::uint64_t firstFed, std::uint64_t offset, std::size_t from = 0)
{
    std::size_t wrong = 0;
    for (std::size_t frame = from; frame < out[0].size(); ++frame) {
        const double expected = static_cast<double>(firstFed + frame) - static_cast<double>(offset);
        if (static_cast<double>(out[0][frame]) != expected || out[1][frame] != 1.0f) {
            if (wrong++ == 0) {
                ADD_FAILURE() << "at " << frame << " the head read frame " << out[0][frame] << " at level "
                              << out[1][frame] << ", not frame " << expected;
            }
        }
    }
    return wrong;
}

/// The loudest a signal gets above 12 kHz, after the filters used to look
/// have settled: where a click shows and a 1 kHz tone does not.
[[nodiscard]] float highestAbove12k(const Signal &signal)
{
    Biquad first;
    Biquad second;
    first.setCoefficients(designBiquad({FilterShape::HighPass, 12000.0, kButterworth4Q1, 0.0}, kTestSampleRate));
    second.setCoefficients(designBiquad({FilterShape::HighPass, 12000.0, kButterworth4Q2, 0.0}, kTestSampleRate));
    float highest = 0.0f;
    for (std::size_t frame = 0; frame < signal.size(); ++frame) {
        const float high = second.process(first.process(signal[frame]));
        if (frame >= 2560) {
            highest = std::max(highest, std::fabs(high));
        }
    }
    return gainToDb(highest);
}

} // namespace

TEST(TimeShiftTest, AtLiveTheOutputIsTheInput)
{
    // Nothing asked for, nothing added: the same samples, no latency and no
    // delay reported.
    const SpillDirectory spill;
    Shifter shifter(kTestSampleRate, 1, spill.path());
    EXPECT_EQ(shifter.block.info().latencySamples, 0U);
    EXPECT_EQ(shifter.block.spill(), nullptr) << "a minute is not kept in memory alone";
    const MultiSignal out = shifter.feed(48000, CycleShape::random(1, 2048, 86));
    EXPECT_EQ(wrongFrames(out, 0, 0), 0U);
    EXPECT_EQ(shifter.block.offsetSamples(), 0U);
    EXPECT_EQ(shifter.block.intendedDelaySamples(), 0U);
    EXPECT_EQ(shifter.meter.value(TimeShift::kOffsetValue), 0.0f);
}

TEST(TimeShiftTest, AJumpPutsTheHeadExactlyWhereItSays)
{
    // The criterion's timing half. Forty seconds kept, then every button
    // the transport has, back and forward: from the fade's end on, every
    // frame out is exactly the frame that far behind live, the intended
    // delay reported is the same number, and so is what the face shows.
    const SpillDirectory spill;
    Shifter shifter(kTestSampleRate, 1, spill.path());
    shifter.skip(40.0);
    const std::size_t fade = shifter.frames(TimeShift::kFadeMs * 1.0e-3);
    for (const double seconds : {30.0, 20.0, 10.0, 3.0, 6.0, 16.0, 13.0, 0.0, 3.0, 33.0, 36.0}) {
        shifter.jump(seconds);
        const std::uint64_t first = shifter.fed();
        const MultiSignal out = shifter.feed(4096, CycleShape::random(1, 2048, 7));
        const std::uint64_t offset = shifter.frames(seconds);
        EXPECT_EQ(wrongFrames(out, first, offset, fade), 0U) << "after the jump to " << seconds << " s";
        EXPECT_EQ(shifter.block.offsetSamples(), offset);
        EXPECT_EQ(shifter.block.intendedDelaySamples(), offset);
        EXPECT_EQ(shifter.meter.value(TimeShift::kOffsetValue), static_cast<float>(seconds));
    }
}

TEST(TimeShiftTest, AJumpFurtherBackThanIsKeptGoesToTheOldest)
{
    // Five seconds kept and ten asked for: five. A minute kept, and a jump
    // to an hour, is a minute.
    const SpillDirectory spill;
    Shifter shifter(kTestSampleRate, 1, spill.path());
    shifter.skip(5.0);
    shifter.jump(10.0);
    const std::uint64_t first = shifter.fed();
    const MultiSignal out = shifter.feed(4096);
    EXPECT_EQ(wrongFrames(out, first, shifter.frames(5.0), 480), 0U);

    shifter.skip(70.0);
    EXPECT_EQ(shifter.block.availableSamples(), shifter.frames(60.0));
    shifter.jump(3600.0);
    const std::uint64_t later = shifter.fed();
    const MultiSignal back = shifter.feed(4096);
    EXPECT_EQ(wrongFrames(back, later, shifter.frames(60.0), 480), 0U);
}

TEST(TimeShiftTest, PausedTheHeadStopsAndPlaysOnFromWhereItStopped)
{
    // A pause fades out and then holds the head while the input goes on
    // being kept; playing again picks up at the very next frame, as far
    // behind live as the pause was long.
    const SpillDirectory spill;
    Shifter shifter(kTestSampleRate, 1, spill.path());
    shifter.skip(2.0);
    shifter.pause(true);
    const MultiSignal paused = shifter.feed(shifter.frames(3.0));
    EXPECT_EQ(shifter.meter.value(TimeShift::kPausedValue), 1.0f);
    shifter.pause(false);
    const MultiSignal playing = shifter.feed(shifter.frames(1.0));

    std::size_t silent = 0;
    float lastHeard = -1.0f;
    for (std::size_t frame = 0; frame < paused[0].size(); ++frame) {
        if (paused[1][frame] == 0.0f) {
            ++silent;
            EXPECT_EQ(paused[0][frame], 0.0f);
        } else {
            EXPECT_EQ(silent, 0U) << "sound after the pause had gone silent";
            lastHeard = std::round(paused[0][frame] / paused[1][frame]);
        }
    }
    EXPECT_EQ(silent, shifter.frames(3.0) - 479) << "the fade out is 10 ms, silence its last step";
    ASSERT_GT(playing[1][0], 0.0f);
    EXPECT_EQ(std::round(playing[0][0] / playing[1][0]), lastHeard + 1.0f) << "a frame was lost or said twice";
    EXPECT_EQ(shifter.block.offsetSamples(), silent);
    EXPECT_EQ(wrongFrames(playing, shifter.fed() - playing[0].size(), silent, 480), 0U);
}

TEST(TimeShiftTest, SwitchingBetweenLiveAndDelayedDoesNotClick)
{
    // The criterion's click half. A 1 kHz tone, and every kind of switch:
    // back (to a point the tone is at another phase), forward, to live,
    // paused and played again. Above 12 kHz the output stays where a
    // 10 ms fade leaves it; a cut would be tens of decibels louder.
    const SpillDirectory spill;
    TimeShift block;
    block.setSpillDirectory(spill.path());
    ASSERT_TRUE(block.setParam("minutes", ParamValue::ofInt(1)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal tone = sine(48000 * 12, 1000.0, kTestSampleRate, 0.5f);
    Signal out;
    std::size_t at = 0;
    std::int64_t jumps = 0;
    const auto play = [&](std::size_t frames) {
        const Signal piece(tone.begin() + static_cast<Signal::difference_type>(at),
                           tone.begin() + static_cast<Signal::difference_type>(at + frames));
        const MultiSignal got = runner.run(MultiSignal{piece, piece}, CycleShape::fixed(256));
        out.insert(out.end(), got[0].begin(), got[0].end());
        at += frames;
    };
    const auto jump = [&](float seconds) {
        EXPECT_TRUE(block.setParam("target", ParamValue::ofFloat(seconds)).has_value());
        EXPECT_TRUE(block.setParam("jump", ParamValue::ofInt(++jumps)).has_value());
    };
    play(48000 * 4);
    jump(3.00013f); // live to delayed, the tone at another phase
    play(24000);
    jump(1.23457f); // forward
    play(24000);
    jump(0.0f); // back to live
    play(24000);
    EXPECT_TRUE(block.setParam("paused", ParamValue::ofBool(true)).has_value());
    play(24000);
    EXPECT_TRUE(block.setParam("paused", ParamValue::ofBool(false)).has_value());
    play(24000);
    jump(2.71828f); // while a pause's delay is kept, further back
    play(24000);
    EXPECT_LT(highestAbove12k(out), -60.0f);
    EXPECT_GT(block.offsetSamples(), 0U);
}

TEST(TimeShiftTest, JumpsIntoAndAcrossTheDiskDoNotClickEither)
{
    // The same tone, with eleven minutes to keep so they spill and a
    // minute and a half of it kept, so the head can go past memory: into the disk's audio (a crossfade, memory to the
    // spill's ring), across it further than the ring reaches (a dip through silence, since the ring moves) and back
    // into memory.
    const SpillDirectory spill;
    TimeShift block;
    block.setSpillDirectory(spill.path());
    block.setSpillMode(TimeShiftSpill::Mode::Stepped);
    ASSERT_TRUE(block.setParam("minutes", ParamValue::ofInt(11)).has_value());
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    const Signal tone = sine(48000, 1000.0, kTestSampleRate, 0.5f);
    Signal out;
    std::int64_t jumps = 0;
    const auto play = [&](std::size_t pieces, bool keep) {
        for (std::size_t piece = 0; piece < pieces; ++piece) {
            const Signal part(tone.begin(), tone.begin() + 4800);
            const MultiSignal got = runner.run(MultiSignal{part, part}, CycleShape::fixed(256));
            if (keep) {
                out.insert(out.end(), got[0].begin(), got[0].end());
            }
            while (block.spill()->step()) { }
        }
    };
    const auto jump = [&](float seconds) {
        EXPECT_TRUE(block.setParam("target", ParamValue::ofFloat(seconds)).has_value());
        EXPECT_TRUE(block.setParam("jump", ParamValue::ofInt(++jumps)).has_value());
    };
    play(10 * 95, false);
    play(10, true);
    jump(90.00013f); // memory to the disk
    play(10, true);
    EXPECT_FALSE(block.jumpPending());
    jump(70.12345f); // across the disk, further than its ring
    play(10, true);
    EXPECT_FALSE(block.jumpPending());
    EXPECT_EQ(block.intendedDelaySamples(), 3365926U);
    jump(3.0f); // back to memory
    play(10, true);
    EXPECT_LT(highestAbove12k(out), -60.0f);
    EXPECT_EQ(block.underruns(), 0U);
}

TEST(TimeShiftTest, ThirtyMinutesAreKeptOnTheDisk)
{
    // The criterion's buffer half, at 8 kHz so the signal still counts
    // exactly. Thirty-one minutes in; the head sent back the whole thirty,
    // read from the file the spill wrote; then to five minutes back, which
    // is the file too and further than its ring reaches (so it dips
    // through silence rather than crossfading); then to 30 s, which is
    // memory. Every frame after each jump is the right one, none arrived
    // late, and the file was never in the directory to be left behind.
    const SpillDirectory spill;
    constexpr double kRate = 8000.0;
    Shifter shifter(kRate, 30, spill.path());
    ASSERT_NE(shifter.block.spill(), nullptr) << "thirty minutes did not spill to disk";
    EXPECT_EQ(spill.files(), 0U) << "the spill file is still in the directory";
    shifter.skip(31.0 * 60.0);
    EXPECT_EQ(shifter.block.availableSamples(), shifter.frames(30.0 * 60.0));

    const std::size_t fade = shifter.frames(TimeShift::kFadeMs * 1.0e-3);
    for (const double seconds : {1800.0, 300.0, 30.0}) {
        (void)shifter.jumpAndWait(seconds);
        const std::uint64_t first = shifter.fed();
        const MultiSignal out = shifter.feed(shifter.frames(20.0));
        EXPECT_EQ(wrongFrames(out, first, shifter.frames(seconds), fade), 0U)
            << "after the jump to " << seconds << " s";
        EXPECT_EQ(shifter.block.intendedDelaySamples(), shifter.frames(seconds));
    }
    EXPECT_EQ(shifter.block.underruns(), 0U);
    EXPECT_EQ(shifter.block.spill()->dropped(), 0U);
}

TEST(TimeShiftTest, PausedLongerThanMemoryPlaysOnFromTheDisk)
{
    // Paused for longer than the recent minute memory holds, the head's
    // audio is on the disk by the time it plays again: the spill has read
    // it while it waited.
    const SpillDirectory spill;
    constexpr double kRate = 8000.0;
    Shifter shifter(kRate, 11, spill.path());
    shifter.skip(10.0);
    shifter.pause(true);
    shifter.skip(90.0);
    shifter.pause(false);
    const std::uint64_t first = shifter.fed();
    const MultiSignal out = shifter.feed(shifter.frames(5.0));
    const std::uint64_t offset = shifter.block.offsetSamples();
    EXPECT_GT(offset, shifter.frames(89.0));
    EXPECT_EQ(wrongFrames(out, first, offset, 80), 0U);
    EXPECT_EQ(shifter.block.underruns(), 0U);
}

TEST(TimeShiftTest, TheSpillsOwnThreadKeepsUp)
{
    // The same, with the spill on the thread of its own that a session
    // runs it on, and the test only a little kinder than a sound card:
    // ten times faster than real time, waiting for nothing but the file.
    const SpillDirectory spill;
    TimeShift block;
    block.setSpillDirectory(spill.path());
    ASSERT_TRUE(block.setParam("minutes", ParamValue::ofInt(11)).has_value());
    BlockRunner runner(block, 2, 2, 8000.0, 2048);
    ASSERT_TRUE(runner.prepare().has_value());
    ASSERT_NE(block.spill(), nullptr);
    std::uint64_t fed = 0;
    const auto feed = [&](std::size_t frames) {
        MultiSignal input{Signal(frames), Signal(frames, 1.0f)};
        for (std::size_t frame = 0; frame < frames; ++frame) {
            input[0][frame] = static_cast<float>(fed + frame);
        }
        fed += frames;
        return runner.run(input, CycleShape::fixed(256));
    };
    const auto wait = [] { std::this_thread::sleep_for(std::chrono::milliseconds(10)); };
    for (int piece = 0; piece < 23; ++piece) {
        (void)feed(32000);
        while (block.spill()->flushed() < fed) {
            wait();
        }
    }
    ASSERT_TRUE(block.setParam("target", ParamValue::ofFloat(80.0f)).has_value());
    ASSERT_TRUE(block.setParam("jump", ParamValue::ofInt(1)).has_value());
    for (int cycle = 0; cycle < 400 && (cycle == 0 || block.jumpPending()); ++cycle) {
        (void)feed(64);
        wait();
    }
    ASSERT_FALSE(block.jumpPending());
    for (int piece = 0; piece < 30; ++piece) {
        const std::uint64_t first = fed;
        const MultiSignal out = feed(800);
        EXPECT_EQ(wrongFrames(out, first, 640000, piece == 0 ? 80 : 0), 0U);
        wait();
    }
    EXPECT_EQ(block.underruns(), 0U);
}

TEST(TimeShiftTest, AHistoryMemoryHoldsHasNoThread)
{
    // Five minutes, the default, is memory's alone: no file and no thread
    // to wake a machine up. Eleven spill, with one thread, gone with the
    // block.
    const SpillDirectory spill;
    const int before = TimeShiftSpill::threadsRunning();
    {
        TimeShift block;
        block.setSpillDirectory(spill.path());
        BlockRunner runner(block, 2, 2, 8000.0, 2048);
        ASSERT_TRUE(runner.prepare().has_value());
        EXPECT_EQ(block.spill(), nullptr);
        EXPECT_EQ(TimeShiftSpill::threadsRunning(), before) << "five minutes started a thread";
    }
    {
        TimeShift block;
        block.setSpillDirectory(spill.path());
        ASSERT_TRUE(block.setParam("minutes", ParamValue::ofInt(11)).has_value());
        BlockRunner runner(block, 2, 2, 8000.0, 2048);
        ASSERT_TRUE(runner.prepare().has_value());
        ASSERT_NE(block.spill(), nullptr);
        // Started before prepare() returns is not promised, only soon after.
        for (int wait = 0; wait < 1000 && TimeShiftSpill::threadsRunning() == before; ++wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        EXPECT_EQ(TimeShiftSpill::threadsRunning(), before + 1);
    }
    EXPECT_EQ(TimeShiftSpill::threadsRunning(), before) << "the spill's thread outlived the block";
}

TEST(TimeShiftSpillTest, ItsThreadSleepsWhenThereIsNothingToDo)
{
    // Caught up, the thread wakes once a kPoll (20 ms) and no more: in
    // 400 ms, twenty times, give or take one either end. It is let go at
    // once when the spill goes, not at the end of a sleep.
    const SpillDirectory directory;
    auto spill = std::make_unique<TimeShiftSpill>(directory.path(), 8000 * 60, 8000, 8000);
    ASSERT_TRUE(spill->ok());
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const std::uint64_t wakeups = spill->wakeups();
    EXPECT_LE(wakeups, 400U / TimeShiftSpill::kPoll.count() + 2) << "the thread is polling, not sleeping";
    EXPECT_GE(wakeups, 1U);
    // Just after a wake, so the thread has all of a sleep ahead of it.
    const std::uint64_t now = spill->wakeups();
    while (spill->wakeups() == now) {
        std::this_thread::yield();
    }
    const auto start = std::chrono::steady_clock::now();
    spill.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, TimeShiftSpill::kPoll / 2)
        << "the spill waited out a sleep to go";
}

TEST(TimeShiftTest, NothingIsTakenOrGivenBackOnTheAudioThread)
{
    // Jumps into memory and into the disk's ring, a pause and a return to
    // live, all watched: the rings and the file were made in prepare().
    const SpillDirectory spill;
    TimeShift block;
    block.setSpillDirectory(spill.path());
    block.setSpillMode(TimeShiftSpill::Mode::Stepped);
    ASSERT_TRUE(block.setParam("minutes", ParamValue::ofInt(11)).has_value());
    BlockRunner runner(block, 2, 2, 8000.0, 2048);
    ASSERT_TRUE(runner.prepare().has_value());
    const auto catchUp = [&] {
        while (block.spill()->step()) { }
    };
    const MultiSignal second = stereo(sine(8000, 440.0, 8000.0, 0.5f));
    for (int piece = 0; piece < 90; ++piece) {
        (void)runner.run(second);
        catchUp();
    }
    std::size_t allocations = 0;
    std::int64_t jumps = 0;
    for (const float seconds : {10.0f, 80.0f, 0.0f}) {
        ASSERT_TRUE(block.setParam("target", ParamValue::ofFloat(seconds)).has_value());
        ASSERT_TRUE(block.setParam("jump", ParamValue::ofInt(++jumps)).has_value());
        for (int piece = 0; piece < 3; ++piece) {
            (void)runner.runWatchingForAllocations(second, CycleShape::random(1, 2048, 5), allocations);
            catchUp();
        }
        EXPECT_FALSE(block.jumpPending());
    }
    ASSERT_TRUE(block.setParam("paused", ParamValue::ofBool(true)).has_value());
    (void)runner.runWatchingForAllocations(second, CycleShape::random(1, 2048, 6), allocations);
    EXPECT_EQ(allocations, 0U);
}

TEST(TimeShiftTest, TheCycleLengthMakesNoDifference)
{
    // The kit's check, at live; and the same at a delay and through a
    // pause, where the jump and the pause come at the same frame in both
    // runs and nothing else may differ.
    const SpillDirectory spill;
    InvarianceOptions options;
    options.inputChannels = 2;
    options.outputChannels = 2;
    const Signal noise = whiteNoise(48000, 0.5f, 3);
    const InvarianceReport report = checkBlockSizeInvariance(
        [&] {
            auto block = std::make_unique<TimeShift>();
            block->setSpillDirectory(spill.path());
            EXPECT_TRUE(block->setParam("minutes", ParamValue::ofInt(1)).has_value());
            return std::unique_ptr<Block>(std::move(block));
        },
        MultiSignal{noise, noise},
        options);
    EXPECT_TRUE(report.passed) << report.summary;

    const auto through = [&](const CycleShape &shape) {
        TimeShift block;
        block.setSpillDirectory(spill.path());
        EXPECT_TRUE(block.setParam("minutes", ParamValue::ofInt(1)).has_value());
        BlockRunner runner(block, 2, 2);
        EXPECT_TRUE(runner.prepare().has_value());
        Signal out = runner.run(MultiSignal{noise, noise}, shape)[0];
        EXPECT_TRUE(block.setParam("target", ParamValue::ofFloat(0.3f)).has_value());
        EXPECT_TRUE(block.setParam("jump", ParamValue::ofInt(1)).has_value());
        const Signal delayed = runner.run(MultiSignal{noise, noise}, shape)[0];
        out.insert(out.end(), delayed.begin(), delayed.end());
        EXPECT_TRUE(block.setParam("paused", ParamValue::ofBool(true)).has_value());
        const Signal paused = runner.run(MultiSignal{noise, noise}, shape)[0];
        out.insert(out.end(), paused.begin(), paused.end());
        return out;
    };
    EXPECT_EQ(through(CycleShape::fixed(256)), through(CycleShape::random(1, 2048, 11)));
}

TEST(TimeShiftTest, WithoutADiskItKeepsWhatMemoryHolds)
{
    // Half an hour asked for and a spill directory nobody can write to: no
    // file, no spill, and the ten minutes memory holds on its own are what
    // can be gone back to.
    TimeShift block;
    block.setSpillDirectory("/proc/rb-timeshift-nowhere");
    ASSERT_TRUE(block.setParam("minutes", ParamValue::ofInt(30)).has_value());
    BlockRunner runner(block, 2, 2, 8000.0, 2048);
    ASSERT_TRUE(runner.prepare().has_value());
    EXPECT_EQ(block.spill(), nullptr);
    const MultiSignal minute = stereo(silence(8000 * 60));
    for (int piece = 0; piece < 11; ++piece) {
        (void)runner.run(minute);
    }
    EXPECT_EQ(block.availableSamples(), static_cast<std::uint64_t>(TimeShift::kMemoryMinutes) * 60U * 8000U);
}

TEST(TimeShiftTest, WhatIsKeptIsSavedButNotWhereTheHeadIs)
{
    // How long the history is and whether it is paused belong to the
    // session; where the head was is gone with the audio it pointed at.
    TimeShift block;
    ASSERT_TRUE(block.setParam("minutes", ParamValue::ofInt(25)).has_value());
    ASSERT_TRUE(block.setParam("paused", ParamValue::ofBool(true)).has_value());
    TimeShift opened;
    ASSERT_TRUE(opened.loadState(block.saveState()).has_value());
    EXPECT_EQ(*opened.param("minutes").asNumber(), 25.0);
    EXPECT_EQ(opened.param("paused").asBool(), std::optional<bool>(true));
    EXPECT_EQ(opened.offsetSamples(), 0U);
    EXPECT_EQ(*TimeShift().param("minutes").asNumber(), static_cast<double>(TimeShift::kDefaultMinutes));
}

TEST(TimeShiftSpillTest, FramesTheDiskCouldNotTakeAreSilenceWhereTheyWere)
{
    // A tap with room for 1000 frames and cycles of 400 that the disk side
    // does not look at: the third and fourth are lost. What reaches the
    // file is the first two, 800 frames of silence, and then the fifth,
    // at the positions the audio thread gave them.
    const SpillDirectory directory;
    TimeShiftSpill spill(directory.path(), 10000, 1000, 4000, TimeShiftSpill::Mode::Stepped);
    ASSERT_TRUE(spill.ok());
    std::array<float, 400> left{};
    std::array<float, 400> right{};
    for (std::uint64_t cycle = 0; cycle < 5; ++cycle) {
        for (std::size_t frame = 0; frame < left.size(); ++frame) {
            left[frame] = static_cast<float>(cycle * 400 + frame + 1);
            right[frame] = -left[frame];
        }
        const std::array<const float *, 2> channels{left.data(), right.data()};
        spill.write(channels, 400);
        if (cycle == 3) {
            while (spill.step()) { }
        }
    }
    while (spill.step()) { }
    EXPECT_EQ(spill.flushed(), 2000U);
    EXPECT_EQ(spill.dropped(), 800U);
    spill.request(0);
    while (spill.step()) { }
    for (std::uint64_t position = 0; position < 2000; ++position) {
        ASSERT_TRUE(spill.has(position)) << position;
        const float expected = position >= 800 && position < 1600 ? 0.0f : static_cast<float>(position + 1);
        ASSERT_EQ(spill.sample(position, 0), expected) << position;
        ASSERT_EQ(spill.sample(position, 1), -expected) << position;
    }
}

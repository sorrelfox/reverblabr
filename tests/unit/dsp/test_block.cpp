// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/audiobuffer.h"
#include "dsp/block.h"
#include "dsp/decibel.h"
#include "dsp/paramvalue.h"
#include "gainblock.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

using namespace rb::dsp;
using rb::test::GainBlock;

namespace
{

/// Runs a block over a whole signal, a cycle at a time.
///
/// This is the smallest thing that will do until the offline graph runner
/// exists: it holds the buffers, hands the block one cycle at a time
/// and gives back everything that came out. It is here rather than in the
/// library on purpose — the library must not grow a test harness.
class OfflineRunner
{
public:
    OfflineRunner(Block &block, std::uint32_t channels, std::uint32_t maxBlockFrames)
        : m_block(block)
        , m_input(channels, maxBlockFrames)
        , m_output(channels, maxBlockFrames)
        , m_channels(channels)
    {
    }

    /// Feeds \a signal to every channel and returns what each channel
    /// produced, in cycles of \a cycleFrames.
    std::vector<std::vector<float>> run(std::span<const float> signal, std::uint32_t cycleFrames)
    {
        std::vector<std::vector<float>> produced(m_channels);
        for (std::size_t offset = 0; offset < signal.size(); offset += cycleFrames) {
            const auto frames = static_cast<std::uint32_t>(std::min<std::size_t>(cycleFrames, signal.size() - offset));
            for (std::uint32_t channel = 0; channel < m_channels; ++channel) {
                const std::span<float> in = m_input.channel(channel);
                for (std::uint32_t frame = 0; frame < frames; ++frame) {
                    in[frame] = signal[offset + frame];
                }
            }
            m_output.clear();

            ProcessContext context{m_input.block(frames), m_output.block(frames), frames};
            m_block.process(context);

            for (std::uint32_t channel = 0; channel < m_channels; ++channel) {
                const std::span<const float> out = m_output.channel(channel);
                produced[channel].insert(produced[channel].end(), out.begin(), out.begin() + frames);
            }
        }
        return produced;
    }

    /// One cycle with no input storage at all, which is an unconnected port.
    std::vector<float> runSilentInput(std::uint32_t frames)
    {
        m_output.clear();
        std::array<float *, 2> noStorage{nullptr, nullptr};
        ProcessContext context{AudioBlock(noStorage.data(), m_channels, frames), m_output.block(frames), frames};
        m_block.process(context);
        const std::span<const float> out = m_output.channel(0);
        return {out.begin(), out.begin() + frames};
    }

    [[nodiscard]] static std::vector<float> constant(std::uint32_t frames, float value)
    {
        return std::vector<float>(frames, value);
    }

private:
    Block &m_block;
    AudioBuffer m_input;
    AudioBuffer m_output;
    std::uint32_t m_channels;
};

PrepareSpec spec(double sampleRate = 48000.0, std::uint32_t maxBlockFrames = 1024)
{
    PrepareSpec prepared;
    prepared.sampleRate = sampleRate;
    prepared.maxBlockFrames = maxBlockFrames;
    prepared.inputChannels = 2;
    prepared.outputChannels = 2;
    return prepared;
}

} // namespace

TEST(Block, DescribesItself)
{
    const GainBlock block;
    EXPECT_EQ(block.info().typeId, "test.gain");
    EXPECT_EQ(block.info().category, BlockCategory::Effects);
    EXPECT_EQ(block.params().size(), 5u);
    EXPECT_TRUE(validateParams(block.params()).has_value()) << "a block's own parameter list has to check out";
}

TEST(Block, PrepareChecksTheSpec)
{
    GainBlock block;
    EXPECT_FALSE(block.isPrepared());
    EXPECT_TRUE(block.prepare(spec()).has_value());
    EXPECT_TRUE(block.isPrepared());

    GainBlock other;
    EXPECT_EQ(other.prepare(spec(0.0)).error(), BlockError::UnsupportedSpec);
    EXPECT_EQ(other.prepare(spec(48000.0, 0)).error(), BlockError::UnsupportedSpec);
    EXPECT_EQ(other.prepare(spec(48000.0, kMaxBlockFrames + 1)).error(), BlockError::UnsupportedSpec);
}

TEST(Block, RunsThroughTheInterfaceAndAppliesItsGain)
{
    GainBlock block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    ASSERT_TRUE(block.setParam("gainDb", ParamValue::ofFloat(-6.0f)).has_value());
    block.reset(); // take the new gain without gliding to it

    OfflineRunner runner(block, 2, 1024);
    const std::vector<float> signal = OfflineRunner::constant(4800, 0.5f);
    const std::vector<std::vector<float>> produced = runner.run(signal, 256);

    ASSERT_EQ(produced.size(), 2u);
    ASSERT_EQ(produced[0].size(), signal.size());
    const float expected = 0.5f * dbToGain(-6.0f);
    EXPECT_NEAR(produced[0].back(), expected, 1.0e-5f);
    EXPECT_NEAR(produced[1].back(), expected, 1.0e-5f);
    EXPECT_GT(block.cycleCount(), 0u);
}

TEST(Block, HandlesEveryCycleLengthTheSameWay)
{
    // PipeWire's quantum changes while a session runs, so the result must not
    // depend on how the signal was chopped up.
    const std::vector<float> signal = OfflineRunner::constant(2048, 0.25f);

    GainBlock reference;
    ASSERT_TRUE(reference.prepare(spec()).has_value());
    reference.reset();
    const std::vector<float> expected = OfflineRunner(reference, 2, 2048).run(signal, 2048)[0];

    for (const std::uint32_t cycleFrames : {kMinBlockFrames, 2u, 31u, 256u, 2048u}) {
        GainBlock block;
        ASSERT_TRUE(block.prepare(spec()).has_value());
        block.reset();
        const std::vector<float> produced = OfflineRunner(block, 2, 2048).run(signal, cycleFrames)[0];
        ASSERT_EQ(produced.size(), expected.size()) << "in cycles of " << cycleFrames;
        for (std::size_t frame = 0; frame < expected.size(); ++frame) {
            EXPECT_NEAR(produced[frame], expected[frame], 1.0e-6f)
                << "in cycles of " << cycleFrames << ", at frame " << frame;
        }
    }
}

TEST(Block, AnInputChannelWithNoStorageIsSilence)
{
    GainBlock block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    OfflineRunner runner(block, 2, 1024);

    const std::vector<float> produced = runner.runSilentInput(128);
    ASSERT_EQ(produced.size(), 128u);
    for (const float sample : produced) {
        EXPECT_EQ(sample, 0.0f);
    }
}

TEST(Block, AGainChangeIsSmoothedRatherThanStepped)
{
    // The parameter arrives through the real-time path, as it would from the
    // parameter queue, and the block glides to it inside process().
    GainBlock block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    block.reset();
    OfflineRunner runner(block, 2, 1024);

    // Settle at unity first.
    static_cast<void>(runner.run(OfflineRunner::constant(1024, 1.0f), 256));

    block.applyParam(GainBlock::GainDb, -20.0f);
    const std::vector<float> produced = runner.run(OfflineRunner::constant(4800, 1.0f), 256)[0];

    float largestStep = 0.0f;
    for (std::size_t frame = 1; frame < produced.size(); ++frame) {
        largestStep = std::max(largestStep, std::abs(produced[frame] - produced[frame - 1]));
    }
    // A 20 ms glide at 48 kHz covers the distance in 960 samples, so no step
    // may be bigger than that distance divided by 960.
    const float distance = 1.0f - dbToGain(-20.0f);
    EXPECT_LT(largestStep, distance / 960.0f * 1.01f) << "the gain stepped instead of gliding";
    EXPECT_NEAR(produced.back(), dbToGain(-20.0f), 1.0e-5f) << "and it did arrive";
}

TEST(Block, TheRealTimePathClampsRatherThanRefusing)
{
    GainBlock block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    block.applyParam(GainBlock::GainDb, 1000.0f);
    EXPECT_FLOAT_EQ(*block.param("gainDb").asFloat(), 12.0f);
}

TEST(Block, MutingTakesTheSignalToSilence)
{
    GainBlock block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    block.reset();
    OfflineRunner runner(block, 2, 1024);
    static_cast<void>(runner.run(OfflineRunner::constant(1024, 1.0f), 256));

    ASSERT_TRUE(block.setParam("muted", ParamValue::ofBool(true)).has_value());
    const std::vector<float> produced = runner.run(OfflineRunner::constant(4800, 1.0f), 256)[0];
    EXPECT_NEAR(produced.back(), 0.0f, 1.0e-6f);
}

TEST(Block, AnEnumChangesWhichChannelsAreTouched)
{
    GainBlock block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    ASSERT_TRUE(block.setParam("gainDb", ParamValue::ofFloat(-20.0f)).has_value());
    ASSERT_TRUE(block.setParam("channels", ParamValue::ofEnum("left")).has_value());
    block.reset();

    OfflineRunner runner(block, 2, 1024);
    const std::vector<std::vector<float>> produced = runner.run(OfflineRunner::constant(2048, 1.0f), 256);
    EXPECT_NEAR(produced[0].back(), dbToGain(-20.0f), 1.0e-5f) << "the left channel was turned down";
    EXPECT_NEAR(produced[1].back(), 1.0f, 1.0e-6f) << "the right channel was left alone";
}

TEST(Block, PublishesItsReadOnlyOutputFromTheAudioThread)
{
    // A gain reduction, an activity light: the block writes it in process()
    // and the telemetry sampler reads it. It is never saved.
    GainBlock block;
    ASSERT_TRUE(block.prepare(spec()).has_value());
    ASSERT_TRUE(block.setParam("gainDb", ParamValue::ofFloat(-9.0f)).has_value());

    OfflineRunner runner(block, 2, 1024);
    static_cast<void>(runner.run(OfflineRunner::constant(256, 1.0f), 256));
    EXPECT_FLOAT_EQ(*block.param("appliedGainDb").asFloat(), -9.0f);

    EXPECT_FALSE(block.saveState().contains("appliedGainDb"));
}

TEST(Block, StateRoundTripsExactlyThroughTheInterface)
{
    GainBlock written;
    ASSERT_TRUE(written.prepare(spec()).has_value());
    ASSERT_TRUE(written.setParam("gainDb", ParamValue::ofFloat(-6.5f)).has_value());
    ASSERT_TRUE(written.setParam("muted", ParamValue::ofBool(true)).has_value());
    ASSERT_TRUE(written.setParam("channels", ParamValue::ofEnum("right")).has_value());
    ASSERT_TRUE(written.setParam("device", ParamValue::ofDeviceRef("alsa_output.usb-Focusrite")).has_value());

    const ParamValueMap state = written.saveState();
    GainBlock read;
    ASSERT_TRUE(read.prepare(spec()).has_value());
    ASSERT_TRUE(read.loadState(state).has_value());

    EXPECT_EQ(read.saveState(), state);
    EXPECT_EQ(read.param("gainDb"), ParamValue::ofFloat(-6.5f));
    EXPECT_EQ(read.param("channels"), ParamValue::ofEnum("right"));
    EXPECT_EQ(read.param("device"), ParamValue::ofDeviceRef("alsa_output.usb-Focusrite"));
}

TEST(Block, AParameterThatDoesNotExistIsReportedRatherThanIgnored)
{
    GainBlock block;
    EXPECT_EQ(block.setParam("nothing", ParamValue::ofFloat(0.0f)).error(), BlockError::UnknownParam);
    EXPECT_TRUE(block.param("nothing").isEmpty());
}

TEST(Block, PreparingForAnotherSampleRateKeepsTheParameters)
{
    // A rate change re-prepares every block. Nothing the user set may be lost.
    GainBlock block;
    ASSERT_TRUE(block.prepare(spec(48000.0)).has_value());
    ASSERT_TRUE(block.setParam("gainDb", ParamValue::ofFloat(-15.0f)).has_value());

    ASSERT_TRUE(block.prepare(spec(96000.0)).has_value());
    EXPECT_EQ(block.param("gainDb"), ParamValue::ofFloat(-15.0f));
}

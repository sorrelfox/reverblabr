// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the sampler works out for the one block being analysed:
// its spectra before and after it, of one channel or both, and its band
// values. Run over a real block in a compiled graph played offline, which
// is the path the audio takes on its way to the rings in a session.

#include "blocks/inputdevice.h"
#include "blocks/lowpassfilter.h"
#include "core/telemetry.h"
#include "dsp/block.h"
#include "graph/compiledgraph.h"
#include "graph/graphspec.h"
#include "rt/meter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <numbers>
#include <random>
#include <vector>

using namespace rb;
using namespace rb::core;

namespace
{

constexpr double kRate = 48000.0;
constexpr std::uint32_t kCycle = 256;

/// One block in a compiled graph, metered, behind an input device the
/// audio system fills: a session's smallest lane.
class OneBlockGraph
{
public:
    OneBlockGraph(dsp::Block &block, rt::BlockMeter &meter)
    {
        dsp::PrepareSpec prepare;
        prepare.sampleRate = kRate;
        prepare.maxBlockFrames = dsp::kMaxBlockFrames;
        prepare.inputChannels = 2;
        prepare.outputChannels = 2;
        EXPECT_TRUE(m_device.prepare(prepare).has_value());
        prepare.meter = &meter;
        EXPECT_TRUE(block.prepare(prepare).has_value());
        meter.setChannelCount(2);

        graph::GraphSpec spec;
        spec.sampleRate = kRate;
        graph::NodeSpec device;
        device.id = "in";
        device.block = &m_device;
        spec.nodes.push_back(device);
        graph::NodeSpec node;
        node.id = "b";
        node.block = &block;
        node.meter = &meter;
        spec.nodes.push_back(node);
        for (std::uint32_t port = 0; port < 2; ++port) {
            graph::EdgeSpec edge;
            edge.fromNode = 0;
            edge.fromPort = port;
            edge.toNode = 1;
            edge.toPort = port;
            spec.edges.push_back(edge);
        }
        auto compiled = graph::CompiledGraph::compile(spec);
        EXPECT_TRUE(compiled.has_value());
        m_graph = std::make_unique<graph::CompiledGraph>(std::move(*compiled));
        for (std::vector<float> &channel : m_inputs) {
            channel.assign(dsp::kMaxBlockFrames, 0.0f);
        }
    }

    /// Runs one cycle, the inputs made by \a fill(channel, frame).
    template <typename Fill>
    void cycle(Fill fill)
    {
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            for (std::uint32_t frame = 0; frame < kCycle; ++frame) {
                m_inputs[channel][frame] = fill(channel, m_position + frame);
            }
            EXPECT_TRUE(m_graph->bindExternalInput(0, channel, m_inputs[channel].data()));
        }
        m_graph->execute(kCycle);
        m_position += kCycle;
    }

private:
    blocks::InputDevice m_device;
    std::unique_ptr<graph::CompiledGraph> m_graph;
    std::array<std::vector<float>, 2> m_inputs;
    std::uint64_t m_position = 0;
};

/// Two seconds of cycles, with the analysis drained every few cycles, as
/// the sampler drains it every 16 ms. Returns the last snapshot.
template <typename Fill>
TelemetrySnapshot play(OneBlockGraph &graph, BlockAnalysis &analysis, Fill fill, double seconds = 2.0)
{
    TelemetrySnapshot snapshot;
    const auto cycles = static_cast<int>(seconds * kRate / kCycle);
    for (int cycle = 0; cycle < cycles; ++cycle) {
        graph.cycle(fill);
        if (cycle % 3 == 2) {
            snapshot = TelemetrySnapshot{};
            analysis.take(kRate, snapshot);
        }
    }
    return snapshot;
}

[[nodiscard]] std::size_t bandAt(double hz)
{
    std::size_t nearest = 0;
    for (std::size_t band = 0; band < kSpectrumBands; ++band) {
        if (std::abs(std::log2(spectrumBandCentreHz(band) / hz))
            < std::abs(std::log2(spectrumBandCentreHz(nearest) / hz))) {
            nearest = band;
        }
    }
    return nearest;
}

[[nodiscard]] std::size_t loudest(const float (&bands)[kSpectrumBands])
{
    return static_cast<std::size_t>(
        std::distance(std::begin(bands), std::max_element(std::begin(bands), std::end(bands))));
}

} // namespace

TEST(BlockAnalysisTest, TheSpectraBeforeAndAfterAFilterDifferByItsResponse)
{
    // A 12 dB/octave low-pass at 1 kHz, fed white noise on both sides: the
    // spectrum after it, less the one before it, is the filter's own
    // response, band by band. Held to the response the block is designed to
    // have, within ±1 dB, wherever the filter leaves enough of the noise to
    // measure: down to 40 dB under.
    blocks::LowPassFilter filter;
    ASSERT_TRUE(filter.setParam("cutoff", dsp::ParamValue::ofFloat(1000.0f)).has_value());
    ASSERT_TRUE(
        filter.setParam("slope", dsp::ParamValue::ofEnum(std::string{blocks::LowPassFilter::kSlope12})).has_value());
    rt::BlockMeter meter;
    OneBlockGraph graph(filter, meter);
    meter.setAnalysing(true);
    BlockAnalysis analysis;
    analysis.follow(&meter, kRate);

    std::mt19937 random(251);
    std::uniform_real_distribution<float> noise(-0.5f, 0.5f);
    const TelemetrySnapshot snapshot =
        play(graph, analysis, [&](std::uint32_t, std::uint64_t) { return noise(random); }, 3.0);

    ASSERT_EQ(snapshot.spectrumBands, kSpectrumBands);
    ASSERT_EQ(snapshot.inputSpectrumBands, kSpectrumBands) << "no spectrum before the block";
    int compared = 0;
    double worst = 0.0;
    for (std::size_t band = 0; band < kSpectrumBands; ++band) {
        const double hz = spectrumBandCentreHz(band);
        const double expected = blocks::LowPassFilter::magnitudeDb(hz, 1000.0, false, kRate);
        if (hz < 50.0 || hz > 16000.0 || expected < -40.0) {
            continue;
        }
        const double measured = static_cast<double>(snapshot.spectrum[band] - snapshot.inputSpectrum[band]);
        EXPECT_NEAR(measured, expected, 1.0) << "at " << hz << " Hz";
        worst = std::max(worst, std::abs(measured - expected));
        ++compared;
    }
    std::printf("[measure] a 1 kHz low-pass, after less before, against its response: %d bands, worst %.2f dB out\n",
                compared,
                worst);
    EXPECT_GT(compared, 50) << "too few bands compared to say anything";
}

TEST(BlockAnalysisTest, OneChannelsSpectraAreOfThatChannelAlone)
{
    // A block that works on one ear is drawn from that ear. A 1 kHz
    // tone on the first side and a 4 kHz tone on the second: each channel's
    // spectrum has its own tone at the top, and both mixed have both.
    blocks::LowPassFilter open; // 20 kHz: the tones pass untouched
    rt::BlockMeter meter;
    OneBlockGraph graph(open, meter);
    meter.setAnalysing(true);
    BlockAnalysis analysis;
    analysis.follow(&meter, kRate);
    const auto tones = [](std::uint32_t channel, std::uint64_t frame) {
        const double hz = channel == 0 ? 1000.0 : 4000.0;
        return static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(frame) / kRate));
    };

    analysis.setChannel(0);
    TelemetrySnapshot first = play(graph, analysis, tones, 1.0);
    EXPECT_EQ(first.analysedChannel, 0);
    EXPECT_EQ(loudest(first.spectrum), bandAt(1000.0));
    EXPECT_EQ(loudest(first.inputSpectrum), bandAt(1000.0));
    EXPECT_LT(first.spectrum[bandAt(4000.0)], first.spectrum[bandAt(1000.0)] - 60.0f) << "the other side leaked in";

    analysis.setChannel(1);
    TelemetrySnapshot second = play(graph, analysis, tones, 1.0);
    EXPECT_EQ(second.analysedChannel, 1);
    EXPECT_EQ(loudest(second.spectrum), bandAt(4000.0));
    EXPECT_LT(second.spectrum[bandAt(1000.0)], second.spectrum[bandAt(4000.0)] - 60.0f) << "the other side leaked in";

    analysis.setChannel(kBothChannels);
    TelemetrySnapshot both = play(graph, analysis, tones, 1.0);
    EXPECT_EQ(both.analysedChannel, kBothChannels);
    EXPECT_NEAR(both.spectrum[bandAt(1000.0)], both.spectrum[bandAt(4000.0)], 1.0f);
}

TEST(BlockAnalysisTest, BandValuesAreTheOnesPublishedSinceTheLookStarted)
{
    // What the block published, whole, and only since this look
    // began: values left from an earlier look are old news, as the rings'
    // contents are.
    rt::BlockMeter meter;
    std::array<float, rt::kMaxBandValues> values{};
    for (std::size_t band = 0; band < values.size(); ++band) {
        values[band] = static_cast<float>(band) * 0.25f;
    }
    meter.setAnalysing(true);
    BlockAnalysis analysis;
    analysis.follow(&meter, kRate);
    TelemetrySnapshot snapshot;
    analysis.take(kRate, snapshot);
    EXPECT_EQ(snapshot.bandValueCount, 0U) << "band values before the block had published any";

    meter.publishBands(values);
    snapshot = TelemetrySnapshot{};
    analysis.take(kRate, snapshot);
    ASSERT_EQ(snapshot.bandValueCount, rt::kMaxBandValues);
    for (std::size_t band = 0; band < values.size(); ++band) {
        EXPECT_FLOAT_EQ(snapshot.bandValues[band], values[band]);
    }

    // A new look at the same block: nothing until it publishes again.
    analysis.follow(&meter, kRate);
    snapshot = TelemetrySnapshot{};
    analysis.take(kRate, snapshot);
    EXPECT_EQ(snapshot.bandValueCount, 0U) << "an earlier look's band values were shown";
}

TEST(BlockAnalysisTest, NothingIsCopiedOrWorkedOutForABlockNobodyAnalyses)
{
    // A block nobody is looking at costs one flag read a cycle. Its
    // rings are never even made, and what it publishes is not stored.
    blocks::LowPassFilter filter;
    rt::BlockMeter meter;
    OneBlockGraph graph(filter, meter);
    std::array<float, rt::kMaxBandValues> values{};
    for (int cycle = 0; cycle < 20; ++cycle) {
        graph.cycle([](std::uint32_t, std::uint64_t) { return 0.25f; });
        meter.publishBands(values);
    }
    EXPECT_EQ(meter.analysis(), nullptr);
    EXPECT_EQ(meter.inputAnalysis(), nullptr);
    EXPECT_EQ(meter.bandPublishCount(), 0U);

    // Looked at once and let go: the rings stay, and nothing goes in them.
    meter.setAnalysing(true);
    graph.cycle([](std::uint32_t, std::uint64_t) { return 0.25f; });
    meter.setAnalysing(false);
    meter.analysis()->discard(meter.analysis()->readableFrames());
    meter.inputAnalysis()->discard(meter.inputAnalysis()->readableFrames());
    for (int cycle = 0; cycle < 20; ++cycle) {
        graph.cycle([](std::uint32_t, std::uint64_t) { return 0.25f; });
        meter.publishBands(values);
    }
    EXPECT_EQ(meter.analysis()->readableFrames(), 0U);
    EXPECT_EQ(meter.inputAnalysis()->readableFrames(), 0U);
    EXPECT_EQ(meter.bandPublishCount(), 0U);
}

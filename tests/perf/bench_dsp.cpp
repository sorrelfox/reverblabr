// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What one block costs, for every block the engine registers: the figure
// the "costs less than N % of a core" tests judge, as a number to keep
// rather than a pass or a fail.
//
// **Every registered block, with its defaults**, so a block added later is
// measured without anyone remembering to add it here. The unit tests that
// hold a block to a budget measure it in the setting that costs it most
// (the FIR filter's longest response, Declick on a worn record); this
// measures what a block costs as it comes out of the library, which is the
// figure that moves when somebody makes the common case slower.
//
// **Only process() is timed**, at 48 kHz in cycles of 256 frames, on pink
// noise at −12 dBFS in every input channel. Each block is run several times
// and the fastest run is kept: the slower ones measured the machine.

#include "blocks/builtinblocks.h"
#include "dsp/audiobuffer.h"
#include "dsp/block.h"
#include "dsp/blockregistry.h"
#include "perfreport.h"
#include "rt/meter.h"
#include "signals.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{

using namespace rb;

constexpr double kRate = 48000.0;
constexpr std::uint32_t kCycle = 256;

struct Options {
    double seconds = 10.0;
    int repeats = 3;
    std::string json;
    std::string only;
    /// A cost added on purpose, as a share of what was measured, so that a
    /// run can prove the report notices a block getting slower.
    double injectPercent = 0.0;
};

void usage()
{
    std::cerr << "usage: reverblabr-bench-dsp [--seconds S] [--repeats N] [--only TEXT] [--json FILE]\n"
                 "                            [--inject-cost PERCENT]\n"
                 "Times every registered block at 48 kHz, 256 frames a cycle, and prints\n"
                 "(or writes) what each costs as a share of one core.\n";
}

[[nodiscard]] bool parse(int argc, char **argv, Options &options)
{
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        const bool hasValue = index + 1 < argc;
        if (argument == "--seconds" && hasValue) {
            options.seconds = std::atof(argv[++index]);
        } else if (argument == "--repeats" && hasValue) {
            options.repeats = std::atoi(argv[++index]);
        } else if (argument == "--json" && hasValue) {
            options.json = argv[++index];
        } else if (argument == "--only" && hasValue) {
            options.only = argv[++index];
        } else if (argument == "--inject-cost" && hasValue) {
            options.injectPercent = std::atof(argv[++index]);
        } else {
            return false;
        }
    }
    return options.seconds > 0.0 && options.repeats > 0;
}

/// The channel count a side of the block is run with: what it says it has,
/// or a stereo pair where it takes a range.
[[nodiscard]] std::uint32_t channelsFor(const dsp::PortSpec &spec)
{
    if (spec.minCount == spec.maxCount) {
        return spec.minCount;
    }
    return std::clamp<std::uint32_t>(2, spec.minCount, spec.maxCount);
}

/// Waits, busily, for \a seconds: the cost a deliberate regression adds.
void spin(double seconds)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until) { }
}

struct Timing {
    bool ran = false;
    std::string why;
    double share = 0.0; ///< Of one core, 0…1.
    std::uint32_t inputs = 0;
    std::uint32_t outputs = 0;
};

[[nodiscard]] Timing timeBlock(const dsp::BlockRegistry &registry,
                               const std::string &typeId,
                               const test::MultiSignal &noise,
                               const Options &options)
{
    Timing timing;
    auto made = registry.create(typeId);
    if (!made) {
        timing.why = "could not be made";
        return timing;
    }
    dsp::Block &block = **made;
    timing.inputs = channelsFor(block.info().inputs);
    timing.outputs = channelsFor(block.info().outputs);

    rt::BlockMeter meter;
    dsp::PrepareSpec spec;
    spec.sampleRate = kRate;
    spec.maxBlockFrames = kCycle;
    spec.inputChannels = timing.inputs;
    spec.outputChannels = timing.outputs;
    // A meter to publish to, as a running session gives every block: a
    // block that measures only for somebody watching would otherwise be
    // timed doing less than it does in a session.
    spec.meter = &meter;
    if (const auto prepared = block.prepare(spec); !prepared) {
        timing.why = "would not prepare with its defaults";
        return timing;
    }

    dsp::AudioBuffer input(std::max<std::uint32_t>(timing.inputs, 1), kCycle);
    dsp::AudioBuffer output(std::max<std::uint32_t>(timing.outputs, 1), kCycle);
    const std::size_t total = noise.front().size();
    const auto cycles = static_cast<std::size_t>(options.seconds * kRate) / kCycle;

    double best = 0.0;
    for (int repeat = 0; repeat < options.repeats; ++repeat) {
        block.reset();
        double took = 0.0;
        std::size_t at = 0;
        for (std::size_t cycle = 0; cycle < cycles; ++cycle) {
            for (std::uint32_t channel = 0; channel < timing.inputs; ++channel) {
                std::memcpy(
                    input.channel(channel).data(), noise[channel % noise.size()].data() + at, kCycle * sizeof(float));
            }
            output.clear();
            dsp::ProcessContext context{input.block(kCycle), output.block(kCycle), kCycle};
            const auto started = std::chrono::steady_clock::now();
            block.process(context);
            took += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            at = (at + kCycle + kCycle <= total) ? at + kCycle : 0;
        }
        if (options.injectPercent > 0.0) {
            const double added = took * options.injectPercent / 100.0;
            spin(added);
            took += added;
        }
        best = repeat == 0 ? took : std::min(best, took);
    }
    timing.ran = true;
    timing.share = best / (static_cast<double>(cycles * kCycle) / kRate);
    return timing;
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        usage();
        return 2;
    }

    dsp::BlockRegistry registry;
    if (!blocks::registerBuiltinBlocks(registry)) {
        std::cerr << "the built-in blocks could not be registered\n";
        return 1;
    }

    // Ten seconds of noise, played round and round: long enough that no
    // block's history lines up with the loop, short enough to make at once.
    const auto frames = static_cast<std::size_t>(kRate * 10);
    const test::MultiSignal noise{test::pinkNoise(frames, 0.25f, 1), test::pinkNoise(frames, 0.25f, 2)};

    perf::Report report("dsp");
    report.note("cycle", "48000 Hz, 256 frames");
    report.note("seconds", std::to_string(options.seconds));
    if (options.injectPercent > 0.0) {
        report.note("injected", std::to_string(options.injectPercent) + " % added to every block on purpose");
    }

    double all = 0.0;
    int measured = 0;
    for (const std::string &typeId : registry.typeIds()) {
        if (!options.only.empty() && typeId.find(options.only) == std::string::npos) {
            continue;
        }
        // **A plugin block runs somebody else's code**, and with no plugin
        // chosen it runs nothing at all: a figure for it would be a figure
        // for an empty function.
        if (typeId.starts_with("plugin.")) {
            report.note("skipped." + typeId, "hosts a plugin, and none is chosen");
            continue;
        }
        const Timing timing = timeBlock(registry, typeId, noise, options);
        if (!timing.ran) {
            std::cout << typeId << ": not measured, it " << timing.why << "\n";
            report.note("skipped." + typeId, timing.why);
            continue;
        }
        const double percent = timing.share * 100.0;
        const double microseconds = timing.share * (kCycle / kRate) * 1e6;
        std::cout << typeId << ": " << percent << " % of a core, " << microseconds << " us a cycle (" << timing.inputs
                  << " in, " << timing.outputs << " out)\n";
        report.add(perf::Metric{
            .name = "dsp." + typeId,
            .value = percent,
            .unit = "% of a core",
            .better = perf::Better::Lower,
            .tolerance = std::nullopt,
            // A fiftieth of a per cent of a core is a microsecond a cycle:
            // below that, a block is as cheap as the timer can tell.
            .noise = 0.02,
            .detail = std::to_string(microseconds) + " us a 256-frame cycle, " + std::to_string(timing.inputs) + " in, "
                + std::to_string(timing.outputs) + " out",
        });
        all += percent;
        ++measured;
    }
    if (measured == 0) {
        std::cerr << "no block was measured\n";
        return 1;
    }
    report.add(perf::Metric{
        .name = "dsp.every-block",
        .value = all,
        .unit = "% of a core",
        .better = perf::Better::Lower,
        .tolerance = std::nullopt,
        .noise = 0.1,
        .detail = "one of each of the " + std::to_string(measured) + " blocks measured, added up",
    });
    std::cout << "every block, one of each: " << all << " % of a core\n";

    if (!options.json.empty() && !report.write(options.json)) {
        std::cerr << "could not write " << options.json << "\n";
        return 1;
    }
    if (options.json.empty() && !report.writeToOutputFolder("dsp.json")) {
        std::cerr << "could not write to RB_PERF_OUT\n";
        return 1;
    }
    return 0;
}

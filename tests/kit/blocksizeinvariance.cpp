// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocksizeinvariance.h"

#include "dsp/decibel.h"
#include "offlinerunner.h"

#include <algorithm>
#include <format>

namespace rb::test
{

InvarianceReport checkBlockSizeInvariance(const std::function<std::unique_ptr<dsp::Block>()> &makeBlock,
                                          const MultiSignal &input,
                                          const InvarianceOptions &options)
{
    InvarianceReport report;

    const auto runOne = [&](const CycleShape &shape) -> MultiSignal {
        std::unique_ptr<dsp::Block> block = makeBlock();
        if (block == nullptr) {
            return {};
        }
        BlockRunner runner(
            *block, options.inputChannels, options.outputChannels, options.sampleRate, options.maxBlockFrames);
        runner.setInputLayout(options.inputLayout);
        runner.setOutputLayout(options.outputLayout);
        runner.setSecondInputFrom(options.secondInputFrom);
        if (!runner.prepare()) {
            return {};
        }
        return runner.run(input, shape);
    };

    const MultiSignal steady = runOne(CycleShape::fixed(options.fixedCycleFrames));
    const MultiSignal varied = runOne(CycleShape::random(dsp::kMinBlockFrames, options.maxBlockFrames, options.seed));

    if (steady.empty() || varied.empty() || steady.size() != varied.size()) {
        report.summary = "the block could not be run";
        return report;
    }

    report.worstNullDb = dsp::kSilenceDb;
    for (std::uint32_t channel = 0; channel < steady.size(); ++channel) {
        if (steady[channel].size() != varied[channel].size()) {
            report.summary = std::format("channel {} came out a different length", channel);
            return report;
        }
        const float difference = nullTestDb(steady[channel], varied[channel]);
        if (difference > report.worstNullDb) {
            report.worstNullDb = difference;
            report.worstChannel = channel;
        }
    }

    report.passed = report.worstNullDb <= options.toleranceDb;
    report.summary = report.passed
        ? std::format("fixed and random cycles agree to {:.1f} dB", report.worstNullDb)
        : std::format(
              "channel {} differs by {:.1f} dB between fixed cycles of {} and random cycles of {}-{}, "
              "which is above the {:.1f} dB allowed",
              report.worstChannel,
              report.worstNullDb,
              options.fixedCycleFrames,
              dsp::kMinBlockFrames,
              options.maxBlockFrames,
              options.toleranceDb);
    return report;
}

} // namespace rb::test

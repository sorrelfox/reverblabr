// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "analysis.h"
#include "dsp/block.h"
#include "dsp/channelmap.h"
#include "signals.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace rb::test
{

/// **Every DSP block has to pass this.**
///
/// PipeWire hands a node whatever cycle length it likes, and changes its mind
/// while the session runs. A block that keeps anything per cycle rather than
/// per sample — a level worked out once at the top, a ramp restarted every
/// call — sounds different depending on the quantum, which is a bug the user
/// hears as the sound changing when nothing changed.
///
/// The check runs the same signal through two fresh blocks: one in fixed
/// cycles, one in cycles drawn at random from across the whole allowed range.
/// The two results have to null against each other.
struct InvarianceReport {
    bool passed = false;
    /// How far apart the two runs were, in decibels, at their worst.
    float worstNullDb = 0.0f;
    /// Which channel that was.
    std::uint32_t worstChannel = 0;
    /// Something a failure message can say.
    std::string summary;
};

/// How the check is run.
struct InvarianceOptions {
    std::uint32_t inputChannels = 1;
    std::uint32_t outputChannels = 1;
    double sampleRate = kTestSampleRate;
    /// The largest cycle to try, and the size of the buffers. The engine's
    /// limit is dsp::kMaxBlockFrames; a smaller one keeps a test quick.
    std::uint32_t maxBlockFrames = 2048;
    std::uint32_t fixedCycleFrames = 256;
    float toleranceDb = kNullToleranceDb;
    std::uint32_t seed = 20260920;
    /// What the input channels carry, for a block that mixes them. Empty
    /// leaves the block to make what it can of the channel count.
    dsp::ChannelLayout inputLayout;
    dsp::ChannelLayout outputLayout;
    /// Where a second group of input channels starts, for a block that
    /// takes audio from two places at once. Zero means one group.
    std::uint32_t secondInputFrom = 0;
};

/// Runs the check. \a makeBlock has to give a **fresh** block each time it is
/// called: the two runs must not share state.
[[nodiscard]] InvarianceReport checkBlockSizeInvariance(const std::function<std::unique_ptr<dsp::Block>()> &makeBlock,
                                                        const MultiSignal &input,
                                                        const InvarianceOptions &options = {});

} // namespace rb::test

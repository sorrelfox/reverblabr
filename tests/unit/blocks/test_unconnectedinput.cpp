// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A block with no wire into it. The graph hands such a port a null channel,
// which reads as silence, so the block's span is empty while the cycle still
// has frames. A block that indexes it brings the whole engine down, and with
// it every running session, so every block is tried here.

#include "blocks/builtinblocks.h"
#include "dsp/audioblock.h"
#include "dsp/block.h"
#include "dsp/blockregistry.h"
#include "offlinerunner.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::uint32_t kFrames = 256;
constexpr int kCycles = 40;

/// Every output of \a block after cycles with nothing on any input; finite.
void runWithNoInput(Block &block, const std::string &typeId)
{
    const std::uint32_t inputs = block.info().inputs.maxCount;
    const std::uint32_t outputs = block.info().outputs.maxCount;
    std::vector<float *> inPointers(inputs, nullptr);
    std::vector<std::vector<float>> storage(outputs, std::vector<float>(kFrames, 1.0f));
    std::vector<float *> outPointers;
    for (std::vector<float> &channel : storage) {
        outPointers.push_back(channel.data());
    }
    ProcessContext context;
    context.input = AudioBlock(inPointers.data(), inputs, kFrames);
    context.output = AudioBlock(outPointers.data(), outputs, kFrames);
    context.frames = kFrames;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        block.process(context);
    }
    for (const std::vector<float> &channel : storage) {
        EXPECT_TRUE(std::ranges::all_of(channel, [](float sample) { return std::isfinite(sample); })) << typeId;
    }
}

} // namespace

TEST(UnconnectedInput, EveryBlockTakesItAsSilence)
{
    BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    std::size_t tried = 0;
    for (const std::string &typeId : registry.typeIds()) {
        auto made = registry.create(typeId);
        ASSERT_TRUE(made.has_value()) << typeId;
        std::unique_ptr<Block> block = std::move(*made);
        const std::uint32_t inputs = block->info().inputs.maxCount;
        if (inputs == 0) {
            continue;
        }
        BlockRunner runner(*block, inputs, block->info().outputs.maxCount);
        if (!runner.prepare().has_value()) {
            continue; // needs a machine this test does not have
        }
        SCOPED_TRACE(typeId);
        runWithNoInput(*block, typeId);
        ++tried;
    }
    EXPECT_GT(tried, 30U);
}

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a block can do to a peak. The last limit is decided from this, so a
// block that says it adds nothing is held to it at full scale.

#include "analysis.h"
#include "blocks/builtinblocks.h"
#include "dsp/block.h"
#include "dsp/blockregistry.h"
#include "dsp/decibel.h"
#include "dsp/paramvalue.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace rb;
using namespace rb::dsp;
using namespace rb::test;

namespace
{

constexpr std::size_t kFrames = 24000;

[[nodiscard]] Signal fullScaleSquare(std::size_t frames, std::size_t period)
{
    Signal signal(frames);
    for (std::size_t n = 0; n < frames; ++n) {
        signal[n] = (n / period) % 2 == 0 ? 1.0f : -1.0f;
    }
    return signal;
}

[[nodiscard]] Signal fullScaleNoise(std::size_t frames, std::uint32_t seed)
{
    Signal signal = whiteNoise(frames, 1.0f, seed);
    const float loudest = peak(signal);
    for (float &sample : signal) {
        sample /= loudest;
    }
    return signal;
}

/// The settings each Unity block is tried at: what moves a gain or a delay.
[[nodiscard]] std::vector<ParamValueMap> settingsOf(const std::string &typeId)
{
    if (typeId == "effect.balance") {
        return {{}, {{"balance", ParamValue::ofFloat(-100.0f)}}, {{"balance", ParamValue::ofFloat(100.0f)}}};
    }
    return {{}};
}

/// The loudest sample out of a fresh block fed \a signal on every input.
[[nodiscard]] float loudestOut(const BlockRegistry &registry,
                               const std::string &typeId,
                               const ParamValueMap &settings,
                               const Signal &signal)
{
    std::unique_ptr<Block> block = std::move(*registry.create(typeId));
    if (!settings.empty()) {
        EXPECT_TRUE(block->loadState(settings).has_value()) << typeId;
    }
    const std::uint32_t inputs = block->info().inputs.maxCount;
    const std::uint32_t outputs = block->info().outputs.maxCount;
    BlockRunner runner(*block, inputs, outputs);
    EXPECT_TRUE(runner.prepare().has_value()) << typeId;
    const MultiSignal out = runner.run(spread(signal, inputs), CycleShape::fixed(256));
    float loudest = 0.0f;
    for (const Signal &channel : out) {
        loudest = std::max(loudest, peak(channel));
    }
    return loudest;
}

} // namespace

TEST(GainReach, EveryBlockIsWhatTheTableSays)
{
    BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    const std::map<std::string, GainReach> named{
        {"hearing.compensation16", GainReach::EndsInLimit},
        {"hearing.compensation64", GainReach::EndsInLimit},
        {"hearing.joinlr", GainReach::Unity},
        {"meter.peakrms", GainReach::Unity},
        {"meter.vu", GainReach::Unity},
        {"meter.speechnow", GainReach::Unity},
        {"meter.voicegauge", GainReach::Unity},
        {"output.recorder", GainReach::Unity},
        {"effect.balance", GainReach::Unity},
        {"advanced.sync", GainReach::Unity},
        {"advanced.timeshift", GainReach::Unity},
    };
    std::set<std::string> found;
    for (const std::string &typeId : registry.typeIds()) {
        const BlockInfo info = *registry.describe(typeId);
        const auto entry = named.find(typeId);
        const GainReach expected = entry == named.end() ? GainReach::Adds : entry->second;
        EXPECT_EQ(info.gainReach, expected) << typeId;
        if (entry != named.end()) {
            found.insert(typeId);
        }
    }
    EXPECT_EQ(found.size(), named.size()) << "a block named here has another id";
}

TEST(GainReach, EveryUnityBlockStaysUnderItsInput)
{
    BlockRegistry registry;
    ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
    const float allowed = dbToGain(0.1f);
    std::size_t tried = 0;
    for (const std::string &typeId : registry.typeIds()) {
        if (registry.describe(typeId)->gainReach != GainReach::Unity) {
            continue;
        }
        ++tried;
        for (const ParamValueMap &settings : settingsOf(typeId)) {
            for (const Signal &signal : {fullScaleNoise(kFrames, 3), fullScaleSquare(kFrames, 50)}) {
                EXPECT_LE(loudestOut(registry, typeId, settings, signal), allowed) << typeId;
            }
        }
    }
    EXPECT_EQ(tried, 9U);
}

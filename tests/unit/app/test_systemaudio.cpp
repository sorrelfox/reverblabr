// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where the System Audio block's face and panel meet the block
// itself: the place in the telemetry the stream count arrives at, and the
// parameters the panel writes into.

#include "blocks/systemaudio.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace rb;

namespace
{

/// Whether the block describes a parameter under that id.
[[nodiscard]] bool describes(const blocks::SystemAudio &block, std::string_view id)
{
    const std::span<const dsp::ParamDescriptor> params = block.params();
    return std::ranges::any_of(params, [id](const dsp::ParamDescriptor &entry) { return entry.id == id; });
}

TEST(SystemAudioFaceTest, TheStreamCountIsTheFirstValueTheBlockPublishes)
{
    // SystemAudioFace.qml reads `values[0]` for the dot that says whether
    // anything is being captured. The interface may not reach into the
    // block layer, so the index is written there by hand; this holds the
    // two together, because a block that moved its count would otherwise
    // leave the tile reading some other number without a word.
    EXPECT_EQ(blocks::SystemAudio::kStreamCountValue, 0U);
}

TEST(SystemAudioPanelTest, WritesTheParametersTheBlockActuallyHas)
{
    // The panel names its two parameters as strings, because that is what
    // setParam takes. A block that renamed one would leave the checkbox
    // writing into nothing, and the panel would go on looking as if it had
    // worked.
    const blocks::SystemAudio block;

    EXPECT_TRUE(describes(block, "includeAlerts"));
    EXPECT_TRUE(describes(block, "excluded"));
}

TEST(SystemAudioPanelTest, AlertSoundsAreOffUntilSomebodyAsksForThem)
{
    // The criterion: the checkbox is off by default, and the default is the
    // block's rather than the panel's — a panel that decided this for
    // itself would disagree with every session nobody has opened it for.
    const blocks::SystemAudio block;

    EXPECT_FALSE(block.param("includeAlerts").asBool().value_or(true));

    const std::optional<std::vector<std::string>> excluded = block.param("excluded").asStringList();
    ASSERT_TRUE(excluded.has_value());
    EXPECT_TRUE(excluded->empty());
}

} // namespace

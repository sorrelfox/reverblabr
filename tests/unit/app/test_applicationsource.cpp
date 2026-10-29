// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Where the Application block's face and panel meet the block itself:
// the place in the telemetry the stream count arrives at, the parameters
// the panel writes into, the spellings of the capture modes, and the one
// rule that the three fields a session stores an application by come to.

#include "blocks/applicationsource.h"
#include "inventory.h"

#include <QString>
#include <QVariantMap>
#include <gtest/gtest.h>

#include <algorithm>
#include <span>
#include <string_view>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// Whether the block describes a parameter under that id.
[[nodiscard]] bool describes(const blocks::ApplicationSource &block, std::string_view id)
{
    const std::span<const dsp::ParamDescriptor> params = block.params();
    return std::ranges::any_of(params, [id](const dsp::ParamDescriptor &entry) { return entry.id == id; });
}

[[nodiscard]] QString asQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

TEST(ApplicationFaceTest, TheStreamCountIsTheFirstValueTheBlockPublishes)
{
    // ApplicationSourceFace.qml reads `values[0]` for the line that says
    // how much is being captured, and for the dot beside it. The interface
    // may not reach into the block layer, so the index is written there by
    // hand; this holds the two together, because a block that moved its
    // count would otherwise leave the tile reading some other number
    // without a word.
    EXPECT_EQ(blocks::ApplicationSource::kStreamCountValue, 0U);
}

TEST(ApplicationPanelTest, WritesTheParametersTheBlockActuallyHas)
{
    // The panel names its parameters as strings, because that is what
    // setParam takes. A block that renamed one would leave a control
    // writing into nothing, and the panel would go on looking as if it had
    // worked.
    const blocks::ApplicationSource block;

    EXPECT_TRUE(describes(block, "binary"));
    EXPECT_TRUE(describes(block, "flatpakId"));
    EXPECT_TRUE(describes(block, "name"));
    EXPECT_TRUE(describes(block, "outputMode"));
    EXPECT_TRUE(describes(block, "outputs"));
    EXPECT_TRUE(describes(block, "includeInput"));
    EXPECT_TRUE(describes(block, "splitChannels"));
    EXPECT_TRUE(describes(block, "includeLowFrequency"));
    EXPECT_TRUE(describes(block, "openUrlOnStart"));
    EXPECT_TRUE(describes(block, "url"));
}

TEST(ApplicationPanelTest, TheCaptureModesAreTheBlocksOwnSpellings)
{
    // ApplicationSourcePanel.qml writes these three words into the session,
    // and the session plan reads them back. They are the block's constants;
    // the interface may not reach into the block layer, so they are written
    // there as well and this holds the two together.
    EXPECT_EQ(asQString(blocks::ApplicationSource::kOutputModeDefault), u"default"_s);
    EXPECT_EQ(asQString(blocks::ApplicationSource::kOutputModeAny), u"any"_s);
    EXPECT_EQ(asQString(blocks::ApplicationSource::kOutputModeChosen), u"chosen"_s);
}

TEST(ApplicationRuleTest, ThreeFieldsBecomeOneRuleInTheOrderTheEngineMatchesThem)
{
    // **The binary wins, then the Flatpak id, then the name**, which is
    // what core::SessionPlan does with them: every field that is set has to
    // match, so it keeps the first and clears the rest. A picker that built
    // the rule in another order would show an application the engine is not
    // looking for.
    const ui::Inventory inventory;

    EXPECT_EQ(inventory.applicationRuleFrom(u"firefox"_s, u"org.mozilla.firefox"_s, u"Firefox"_s), u"binary:firefox"_s);
    EXPECT_EQ(inventory.applicationRuleFrom(QString(), u"org.videolan.VLC"_s, u"VLC"_s), u"flatpak:org.videolan.VLC"_s);
    EXPECT_EQ(inventory.applicationRuleFrom(QString(), QString(), u"Music Player"_s), u"name:Music Player"_s);
    EXPECT_EQ(inventory.applicationRuleFrom(QString(), QString(), QString()), QString());
}

TEST(ApplicationRuleTest, ARuleBecomesTheThreeFieldsAndClearsTheOtherTwo)
{
    // A block pointed at something new has to stop matching what it was
    // pointed at before, so all three fields come back every time and two
    // of them are empty.
    const ui::Inventory inventory;

    const QVariantMap binary = inventory.applicationFields(u"binary:firefox"_s);
    EXPECT_EQ(binary.value(u"binary"_s).toString(), u"firefox"_s);
    EXPECT_TRUE(binary.value(u"flatpakId"_s).toString().isEmpty());
    EXPECT_TRUE(binary.value(u"name"_s).toString().isEmpty());

    const QVariantMap flatpak = inventory.applicationFields(u"flatpak:org.videolan.VLC"_s);
    EXPECT_EQ(flatpak.value(u"flatpakId"_s).toString(), u"org.videolan.VLC"_s);
    EXPECT_TRUE(flatpak.value(u"binary"_s).toString().isEmpty());

    const QVariantMap named = inventory.applicationFields(u"name:Music Player"_s);
    EXPECT_EQ(named.value(u"name"_s).toString(), u"Music Player"_s);
    EXPECT_TRUE(named.value(u"binary"_s).toString().isEmpty());

    // What somebody types is a program's name, which is what `binary:`
    // means — the same answer the picker's own rule gives.
    EXPECT_EQ(inventory.applicationFields(u"obs"_s).value(u"binary"_s).toString(), u"obs"_s);
}

TEST(ApplicationRuleTest, ANodeScopedRuleNamesNoApplicationAtAll)
{
    // **A node is one stream on one boot.** An exclusion is matched while
    // the session runs, so `node:` is a rule like any other there; a block
    // pointed at one would be pointed at nothing tomorrow, so it names none
    // of the three fields and the source picker has something to refuse.
    const ui::Inventory inventory;

    const QVariantMap fields = inventory.applicationFields(u"node:alsa_playback.someone"_s);
    EXPECT_TRUE(fields.value(u"binary"_s).toString().isEmpty());
    EXPECT_TRUE(fields.value(u"flatpakId"_s).toString().isEmpty());
    EXPECT_TRUE(fields.value(u"name"_s).toString().isEmpty());
    // And the rule itself is still well formed, which is why it has to be
    // refused deliberately rather than by failing to parse.
    EXPECT_EQ(inventory.applicationRule(u"node:alsa_playback.someone"_s), u"node:alsa_playback.someone"_s);
}

TEST(ApplicationRuleTest, TheTwoDirectionsAgreeWithOneAnother)
{
    // A rule that came out of the picker, written into the three fields and
    // read back as a rule, has to be the rule it started as — which is what
    // the panel and the face both depend on.
    const ui::Inventory inventory;

    for (const QString &rule : {u"binary:firefox"_s, u"flatpak:org.videolan.VLC"_s, u"name:Music Player"_s}) {
        const QVariantMap fields = inventory.applicationFields(rule);
        EXPECT_EQ(inventory.applicationRuleFrom(fields.value(u"binary"_s).toString(),
                                                fields.value(u"flatpakId"_s).toString(),
                                                fields.value(u"name"_s).toString()),
                  rule);
    }
}

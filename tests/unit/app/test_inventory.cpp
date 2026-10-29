// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The device lists the two device panels draw from: the live
// sinks and sources, what the defaults resolve to, and each device's own
// channels. The applications the exclusion picker draws from, which
// are named by match rule rather than by anything that lasts only as long
// as a stream does. And the other half of "unavailable": what a block
// publishes about its device, on its way from the engine's snapshot to the
// panel.

#include "appshell.h"
#include "core/mockengine.h"
#include "core/telemetry.h"
#include "inventory.h"
#include "meters.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// An inventory following its own mock, with the made-up devices in it.
class Following
{
public:
    Following()
    {
        inventory.attach(&engine);
        engine.addFakeInventory();
    }

    core::MockEngine engine;
    ui::Inventory inventory;
};

[[nodiscard]] QStringList nodesOf(const QVariantList &devices)
{
    QStringList nodes;
    for (const QVariant &device : devices) {
        nodes.append(device.toMap().value(u"node"_s).toString());
    }
    return nodes;
}

TEST(InventoryTest, SaysNothingUntilTheEngineHasAnswered)
{
    // A picker opened before the engine has answered offers nothing rather
    // than claiming the machine has no devices at all.
    ui::Inventory inventory;
    EXPECT_FALSE(inventory.isReady());
    EXPECT_TRUE(inventory.outputs().isEmpty());
    EXPECT_TRUE(inventory.inputs().isEmpty());
}

TEST(InventoryTest, HoldsTheLiveSinksAndSourcesTheEngineSent)
{
    Following following;

    EXPECT_TRUE(following.inventory.isReady());
    // In the order the engine sent them, which is the order it sorted them
    // into; the picker does not sort them again.
    EXPECT_EQ(nodesOf(following.inventory.outputs()), (QStringList{u"alsa_output.hdmi"_s, u"alsa_output.analog"_s}));
    EXPECT_EQ(nodesOf(following.inventory.inputs()), (QStringList{u"alsa_input.usb-Microphone"_s}));
    EXPECT_TRUE(following.inventory.hasOutput(u"alsa_output.hdmi"_s));
    EXPECT_FALSE(following.inventory.hasOutput(u"alsa_input.usb-Microphone"_s));
    EXPECT_TRUE(following.inventory.hasInput(u"alsa_input.usb-Microphone"_s));
}

TEST(InventoryTest, ResolvesTheDefaultToWhateverTheSystemHasMadeIt)
{
    Following following;

    EXPECT_EQ(following.inventory.defaultOutput(), u"alsa_output.analog"_s);
    EXPECT_EQ(following.inventory.defaultInput(), u"alsa_input.usb-Microphone"_s);
    // **"Default output" is a reference, not a name.** What a face puts
    // under it is the device the reference lands on at the moment.
    EXPECT_EQ(following.inventory.outputName(following.inventory.defaultDevice()), u"Headphones"_s);
    EXPECT_EQ(following.inventory.inputName(following.inventory.defaultDevice()), u"USB Microphone"_s);
    EXPECT_TRUE(following.inventory.hasOutput(following.inventory.defaultDevice()));
}

TEST(InventoryTest, SpellsTheDefaultReferenceTheWayTheBlocksDo)
{
    // The interface may not reach into the block layer, so the one word is
    // written in both places; if it ever stops matching, a session that
    // follows the default stops following it and nothing else says so.
    ui::Inventory inventory;
    EXPECT_EQ(inventory.defaultDevice(), u"default"_s);
}

TEST(InventoryTest, KnowsEachDevicesOwnChannels)
{
    Following following;

    // The criterion: the channel combos list *that* device's channels.
    EXPECT_EQ(following.inventory.outputChannels(u"alsa_output.hdmi"_s),
              (QStringList{u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s}));
    EXPECT_EQ(following.inventory.outputChannels(u"alsa_output.analog"_s), (QStringList{u"FL"_s, u"FR"_s}));
    EXPECT_EQ(following.inventory.inputChannels(u"alsa_input.usb-Microphone"_s), (QStringList{u"MONO"_s}));
    EXPECT_TRUE(following.inventory.outputChannels(u"alsa_output.nothing-like-a-device"_s).isEmpty());
}

TEST(InventoryTest, ListsTheApplicationsThatArePlaying)
{
    Following following;

    // The criterion behind the exclusion picker: what it offers is what the
    // engine says is playing, each by its match rule.
    const QVariantList applications = following.inventory.applications();
    ASSERT_EQ(applications.size(), 2);
    EXPECT_EQ(applications.at(0).toMap().value(u"key"_s).toString(), u"binary:firefox"_s);
    EXPECT_EQ(applications.at(0).toMap().value(u"name"_s).toString(), u"Firefox"_s);
    EXPECT_EQ(following.inventory.application(u"binary:musicplayer"_s).value(u"name"_s).toString(), u"Music Player"_s);
    EXPECT_TRUE(following.inventory.hasApplication(u"binary:firefox"_s));
    EXPECT_FALSE(following.inventory.hasApplication(u"binary:nothing-is-playing-this"_s));
}

TEST(InventoryTest, NamesAnApplicationThatIsNotPlayingByItsRule)
{
    Following following;

    // **An exclusion outlives the program it names**, which is the whole
    // reason a rule is stored rather than a stream. Nothing can say what a
    // program calls itself while it is closed, so the rule's own subject is
    // shown — which is what somebody typed in the first place.
    EXPECT_EQ(following.inventory.applicationName(u"binary:firefox"_s), u"Firefox"_s);
    EXPECT_EQ(following.inventory.applicationName(u"flatpak:org.example.Player"_s), u"org.example.Player"_s);
    EXPECT_EQ(following.inventory.applicationName(u"binary:obs"_s), u"obs"_s);
    // Something with no rule prefix at all is its own name rather than an
    // empty row: a session from a later version still says something.
    EXPECT_EQ(following.inventory.applicationName(u"whatever-this-is"_s), u"whatever-this-is"_s);
}

TEST(InventoryTest, MakesAMatchRuleOutOfWhatSomebodyTyped)
{
    Following following;

    // A name typed into the picker is the name of a program, which is what
    // a binary rule means.
    EXPECT_EQ(following.inventory.applicationRule(u"  obs  "_s), u"binary:obs"_s);
    // A rule pasted in is already one, and wrapping it in a second would
    // match nothing at all.
    EXPECT_EQ(following.inventory.applicationRule(u"flatpak:org.example.Player"_s), u"flatpak:org.example.Player"_s);
    EXPECT_EQ(following.inventory.applicationRule(u"name:Music Player"_s), u"name:Music Player"_s);
    EXPECT_EQ(following.inventory.applicationRule(u"node:some.node"_s), u"node:some.node"_s);
    // Nothing typed is nothing to exclude.
    EXPECT_TRUE(following.inventory.applicationRule(u"   "_s).isEmpty());
}

TEST(InventoryTest, NamesADeviceNobodyHasPluggedIn)
{
    Following following;

    // A session that names a device which is not there is not a broken
    // session: the node name is the honest thing to show until it comes
    // back, and it is what the picker matches on when it does.
    EXPECT_EQ(following.inventory.outputName(u"alsa_output.usb-interface"_s), u"alsa_output.usb-interface"_s);
    EXPECT_FALSE(following.inventory.hasOutput(u"alsa_output.usb-interface"_s));
}

TEST(InventoryTest, FollowsADeviceGoingAwayAndComingBack)
{
    Following following;
    QSignalSpy changed(&following.inventory, &ui::Inventory::changed);
    const qulonglong before = following.inventory.revision();

    following.engine.setFakeDevicePresent(u"alsa_output.hdmi"_s, false);
    EXPECT_EQ(changed.count(), 1);
    EXPECT_GT(following.inventory.revision(), before);
    EXPECT_FALSE(following.inventory.hasOutput(u"alsa_output.hdmi"_s));
    EXPECT_EQ(nodesOf(following.inventory.outputs()), (QStringList{u"alsa_output.analog"_s}));

    following.engine.setFakeDevicePresent(u"alsa_output.hdmi"_s, true);
    EXPECT_EQ(changed.count(), 2);
    EXPECT_TRUE(following.inventory.hasOutput(u"alsa_output.hdmi"_s));
    // And it comes back with everything it had, channels included.
    EXPECT_EQ(following.inventory.outputChannels(u"alsa_output.hdmi"_s).size(), 6);
}

TEST(InventoryTest, ADefaultThatIsNotThereResolvesToNothing)
{
    Following following;

    following.engine.setFakeDevicePresent(u"alsa_output.analog"_s, false);
    EXPECT_TRUE(following.inventory.defaultOutput().isEmpty());
    // Which is what makes a block that follows the default read as
    // unavailable rather than as pointing at a device that is still there.
    EXPECT_FALSE(following.inventory.hasOutput(following.inventory.defaultDevice()));
}

TEST(InventoryTest, KnowsAHeadsetThatIsOnItsPlaybackProfile)
{
    // The headset's microphone is listed in both profiles; only in the
    // playback one does it record nothing.
    Following following;
    const QString headset = u"bluez_input.00:1B:66:A1:B2:C3"_s;
    EXPECT_FALSE(following.inventory.inputOnPlaybackProfile(headset));

    following.engine.addFakeHeadset(true);
    EXPECT_TRUE(following.inventory.hasInput(headset));
    EXPECT_TRUE(following.inventory.inputOnPlaybackProfile(headset));
    EXPECT_TRUE(following.inventory.input(headset).value(u"wireless"_s).toBool());
    EXPECT_FALSE(following.inventory.inputOnPlaybackProfile(u"alsa_input.usb-Microphone"_s));

    following.engine.addFakeHeadset(false);
    EXPECT_TRUE(following.inventory.hasInput(headset));
    EXPECT_FALSE(following.inventory.inputOnPlaybackProfile(headset));
    EXPECT_EQ(nodesOf(following.inventory.inputs()).count(headset), 1) << "switching the profile listed it twice";
}

TEST(InventoryTest, ReadsAnInventoryThatDidNotComeFromAnEngine)
{
    ui::Inventory inventory;
    inventory.setInventory(QJsonDocument::fromJson(R"({
        "outputs": [{"node": "made.up", "description": "Made Up", "channels": ["FL", "FR", "AUX3"]}],
        "inputs": [],
        "defaults": {"sink": "made.up", "source": ""}
    })")
                               .object());

    EXPECT_TRUE(inventory.isReady());
    EXPECT_EQ(inventory.outputName(u"made.up"_s), u"Made Up"_s);
    EXPECT_EQ(inventory.outputChannels(u"made.up"_s).back(), u"AUX3"_s);
    EXPECT_TRUE(inventory.inputName(inventory.defaultDevice()).isEmpty());
}

/// A snapshot as the engine's sampler publishes one, with one block in it.
class Published
{
public:
    explicit Published(const char *blockId)
    {
        core::BlockTelemetry &block = m_snapshot.blocks[0];
        std::strncpy(block.id, blockId, core::kMaxTelemetryIdBytes - 1);
        block.channelCount = 2;
        m_snapshot.blockCount = 1;
        m_snapshot.running = true;
    }

    void publishDevicePresent(bool present)
    {
        // rb::blocks::OutputDevice::kDevicePresentValue, which the interface
        // may not include: the first of the values a device block publishes.
        m_snapshot.blocks[0].values[0] = present ? 1.0f : 0.0f;
        ++m_snapshot.frame;
        m_slot.store(m_snapshot);
    }

    [[nodiscard]] const core::TelemetrySlot *slot() const
    {
        return &m_slot;
    }

private:
    core::TelemetrySnapshot m_snapshot;
    core::TelemetrySlot m_slot;
};

/// **A timer needs an event loop to exist in**, and the meters read on one.
class MetersTest : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        s_argv = {s_name.data(), nullptr};
        s_argc = 1;
        s_application = std::make_unique<QCoreApplication>(s_argc, s_argv.data());
    }

    static void TearDownTestSuite()
    {
        s_application.reset();
    }

    void TearDown() override
    {
        // The engine is process-wide, and a dangling one would be the next
        // test's problem rather than this one's.
        ui::AppShell::setEngine(nullptr);
    }

private:
    static std::array<char, 8> s_name;
    static std::array<char *, 2> s_argv;
    static int s_argc;
    static std::unique_ptr<QCoreApplication> s_application;
};

std::array<char, 8> MetersTest::s_name{'m', 'e', 't', 'e', 'r', 's', '\0'};
std::array<char *, 2> MetersTest::s_argv{};
int MetersTest::s_argc = 0;
std::unique_ptr<QCoreApplication> MetersTest::s_application;

TEST_F(MetersTest, CarriesWhatABlockPublishesAboutItself)
{
    // The other half of "unavailable": while the session runs, whether the
    // device is there is something the engine knows and publishes, and the
    // panel has to be able to read it.
    core::MockEngine engine;
    Published published("out");
    ui::AppShell::setEngine(&engine, [&published](const QString &) { return published.slot(); });
    published.publishDevicePresent(true);

    ui::Meters meters;
    meters.watch(u"session"_s);
    meters.hub()->tick();

    const QVariantMap levels = meters.levels(u"out"_s);
    ASSERT_FALSE(levels.isEmpty());
    const QList<qreal> values = levels.value(u"values"_s).value<QList<qreal>>();
    ASSERT_FALSE(values.isEmpty());
    EXPECT_DOUBLE_EQ(values.at(0), 1.0);

    published.publishDevicePresent(false);
    meters.hub()->tick();
    EXPECT_DOUBLE_EQ(meters.levels(u"out"_s).value(u"values"_s).value<QList<qreal>>().at(0), 0.0);
}

} // namespace

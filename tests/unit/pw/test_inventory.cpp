// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The inventory against graphs written out by hand. What it does to a
// graph PipeWire actually arranged is tests/pw/test_inventory.cpp; here the
// graph is made up, so a case that is awkward to arrange — a Flatpak, an
// alert, a name nobody set — costs nothing.

#include "pw/inventory.h"
#include "pwdump.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace Qt::StringLiterals;
using namespace rb::pw;

namespace
{

std::uint32_t nextId = 1;

[[nodiscard]] PwNode node(const QString &name, const QString &mediaClass)
{
    PwNode made;
    made.object.id = nextId++;
    made.object.serial = made.object.id * 100;
    made.name = name;
    made.mediaClass = mediaClass;
    made.kind = nodeKindFromMediaClass(mediaClass);
    return made;
}

[[nodiscard]] PwNode stream(const QString &name, const QString &applicationName, const QString &binary)
{
    PwNode made = node(name, u"Stream/Output/Audio"_s);
    made.applicationName = applicationName;
    made.applicationBinary = binary;
    made.props.insert(u"media.name"_s, applicationName);
    return made;
}

[[nodiscard]] PwPort port(const PwNode &owner, PortDirection direction, std::uint32_t index, const QString &channel)
{
    PwPort made;
    made.object.id = nextId++;
    made.object.serial = made.object.id * 100;
    made.nodeId = owner.object.id;
    made.direction = direction;
    made.index = index;
    made.channel = channel;
    made.name = (direction == PortDirection::Input ? u"playback_"_s : u"capture_"_s) + channel;
    return made;
}

[[nodiscard]] PwLink link(const PwNode &from, const PwNode &to)
{
    PwLink made;
    made.object.id = nextId++;
    made.object.serial = made.object.id * 100;
    made.outputNode = from.object.id;
    made.inputNode = to.object.id;
    return made;
}

/// A sink with a stereo pair of input ports.
[[nodiscard]] PwNode sink(const QString &name, const QString &description, QList<PwPort> &ports)
{
    PwNode made = node(name, u"Audio/Sink"_s);
    made.description = description;
    ports.append(port(made, PortDirection::Input, 0, u"FL"_s));
    ports.append(port(made, PortDirection::Input, 1, u"FR"_s));
    return made;
}

[[nodiscard]] const InventoryDevice *deviceNamed(const QList<InventoryDevice> &devices, const QString &name)
{
    for (const InventoryDevice &device : devices) {
        if (device.name == name) {
            return &device;
        }
    }
    return nullptr;
}

/// The inventory of a graph written out the way `pw-dump` writes one.
[[nodiscard]] Inventory inventoryOfDump(const QString &file)
{
    const rb::test::PwDump dump = rb::test::readPwDump(QStringLiteral(RB_PW_GRAPHS_DIR) + u'/' + file);
    return inventoryOf(dump.nodes, dump.ports, dump.links, {}, dump.devices);
}

[[nodiscard]] const InventoryApp *appNamed(const Inventory &inventory, const QString &displayName)
{
    for (const InventoryApp &application : inventory.applications) {
        if (application.displayName == displayName) {
            return &application;
        }
    }
    return nullptr;
}

} // namespace

TEST(InventoryTest, TwoStreamsOfOneApplicationAreOneApplication)
{
    QList<PwPort> ports;
    const PwNode speakers = sink(u"alsa.speakers"_s, u"Speakers"_s, ports);
    PwNode first = stream(u"firefox.one"_s, u"Firefox"_s, u"firefox"_s);
    first.props.insert(u"media.name"_s, u"A video"_s);
    PwNode second = stream(u"firefox.two"_s, u"Firefox"_s, u"firefox"_s);
    second.props.insert(u"media.name"_s, u"Another tab"_s);

    const Inventory inventory =
        inventoryOf({speakers, first, second}, ports, {link(first, speakers), link(second, speakers)}, {});

    ASSERT_EQ(inventory.applications.size(), 1);
    const InventoryApp &firefox = inventory.applications.constFirst();
    EXPECT_EQ(firefox.streamCount(), 2);
    EXPECT_EQ(firefox.key, u"binary:firefox"_s);
    EXPECT_EQ(firefox.displayName, u"Firefox"_s);
    // The sink is named once, however many streams reach it.
    EXPECT_EQ(firefox.sinks, QStringList{u"alsa.speakers"_s});
    // Each stream keeps its own media.name, which is what a per-tab picker
    // will have to work from.
    EXPECT_EQ(firefox.streams.at(0).mediaName, u"A video"_s);
    EXPECT_EQ(firefox.streams.at(1).mediaName, u"Another tab"_s);
}

TEST(InventoryTest, ApplicationsAreToldApartByBinaryThenFlatpakThenName)
{
    const PwNode byBinary = stream(u"a"_s, u"Player"_s, u"mpv"_s);
    PwNode byFlatpak = stream(u"b"_s, u"Video"_s, QString{});
    byFlatpak.flatpakId = u"org.videolan.VLC"_s;
    const PwNode byName = stream(u"c"_s, u"Some Player"_s, QString{});
    const PwNode byNothing = node(u"d"_s, u"Stream/Output/Audio"_s);

    const Inventory inventory = inventoryOf({byBinary, byFlatpak, byName, byNothing}, {}, {}, {});

    ASSERT_EQ(inventory.applications.size(), 4);
    EXPECT_EQ(appNamed(inventory, u"Player"_s)->key, u"binary:mpv"_s);
    EXPECT_EQ(appNamed(inventory, u"Video"_s)->key, u"flatpak:org.videolan.VLC"_s);
    EXPECT_EQ(appNamed(inventory, u"Some Player"_s)->key, u"name:Some Player"_s);
    // Nothing to go on but the node's own name, which at least holds still.
    EXPECT_EQ(appNamed(inventory, u"d"_s)->key, u"node:d"_s);
}

TEST(InventoryTest, OurOwnNodesAreInNoList)
{
    QList<PwPort> ports;
    PwNode ourSink = sink(u"reverblabr.session"_s, u"A session of ours"_s, ports);
    ourSink.isOurs = true;
    PwNode ourSource = node(u"reverblabr.session.out"_s, u"Audio/Source/Virtual"_s);
    ourSource.isOurs = true;
    PwNode ourStream = stream(u"reverblabr.play"_s, u"Reverblabr"_s, u"reverblabr"_s);
    ourStream.isOurs = true;
    const PwNode theirs = stream(u"mpv.one"_s, u"mpv"_s, u"mpv"_s);

    const Inventory inventory = inventoryOf({ourSink, ourSource, ourStream, theirs}, ports, {}, {});

    EXPECT_TRUE(inventory.inputs.isEmpty());
    EXPECT_TRUE(inventory.outputs.isEmpty());
    EXPECT_TRUE(inventory.otherStreams.isEmpty());
    ASSERT_EQ(inventory.applications.size(), 1);
    EXPECT_EQ(inventory.applications.constFirst().displayName, u"mpv"_s);
}

TEST(InventoryTest, AStreamIntoOneOfOurNodesIsNotASinkItReaches)
{
    QList<PwPort> ports;
    PwNode ours = sink(u"reverblabr.session"_s, u"A session of ours"_s, ports);
    ours.isOurs = true;
    const PwNode speakers = sink(u"alsa.speakers"_s, u"Speakers"_s, ports);
    const PwNode playing = stream(u"mpv.one"_s, u"mpv"_s, u"mpv"_s);

    const Inventory inventory =
        inventoryOf({ours, speakers, playing}, ports, {link(playing, ours), link(playing, speakers)}, {});

    ASSERT_EQ(inventory.applications.size(), 1);
    EXPECT_EQ(inventory.applications.constFirst().sinks, QStringList{u"alsa.speakers"_s});
}

TEST(InventoryTest, ReplaysAndAlertsAreNotApplications)
{
    PwNode loopback = stream(u"loopback.out"_s, u"Loopback"_s, u"pw-loopback"_s);
    loopback.linkGroup = u"loopback-1"_s;
    PwNode alert = stream(u"alert"_s, u"Plasma"_s, u"plasmashell"_s);
    alert.mediaRole = u"Notification"_s;
    PwNode alertByEvent = stream(u"alert.two"_s, u"Plasma"_s, u"plasmashell"_s);
    alertByEvent.eventId = u"message-new-instant"_s;
    const PwNode ordinary = stream(u"mpv.one"_s, u"mpv"_s, u"mpv"_s);

    const Inventory inventory = inventoryOf({loopback, alert, alertByEvent, ordinary}, {}, {}, {});

    ASSERT_EQ(inventory.applications.size(), 1);
    EXPECT_EQ(inventory.applications.constFirst().displayName, u"mpv"_s);
    ASSERT_EQ(inventory.otherStreams.size(), 3);
    EXPECT_EQ(inventory.otherStreams.at(0).kind, StreamKind::Replay);
    EXPECT_EQ(inventory.otherStreams.at(1).kind, StreamKind::Alert);
    EXPECT_EQ(inventory.otherStreams.at(2).kind, StreamKind::Alert);
}

TEST(InventoryTest, DevicesCarryTheirChannelsInPortOrder)
{
    QList<PwPort> ports;
    PwNode surround = node(u"alsa.surround"_s, u"Audio/Sink"_s);
    surround.description = u"Surround"_s;
    // Announced out of order, as the server may: the port's own index is
    // what says which channel comes first.
    ports.append(port(surround, PortDirection::Input, 2, u"FC"_s));
    ports.append(port(surround, PortDirection::Input, 0, u"FL"_s));
    ports.append(port(surround, PortDirection::Input, 1, u"FR"_s));
    // A sink's monitor ports are outputs and say nothing about what it takes.
    PwPort monitor = port(surround, PortDirection::Output, 0, u"FL"_s);
    monitor.isMonitor = true;
    ports.append(monitor);
    // Control ports name no channel and are not audio.
    ports.append(port(surround, PortDirection::Input, 9, QString{}));

    PwNode microphone = node(u"alsa.microphone"_s, u"Audio/Source"_s);
    microphone.description = u"Microphone"_s;
    ports.append(port(microphone, PortDirection::Output, 0, u"MONO"_s));

    const Inventory inventory = inventoryOf({surround, microphone}, ports, {}, {});

    ASSERT_EQ(inventory.outputs.size(), 1);
    EXPECT_EQ(inventory.outputs.constFirst().channels, (QStringList{u"FL"_s, u"FR"_s, u"FC"_s}));
    EXPECT_EQ(inventory.outputs.constFirst().channelCount(), 3);
    ASSERT_EQ(inventory.inputs.size(), 1);
    EXPECT_EQ(inventory.inputs.constFirst().channels, QStringList{u"MONO"_s});
}

TEST(InventoryTest, AVirtualSourcesOwnOutputsAreItsChannels)
{
    // A virtual source flags its real output ports as monitors ([ADR-0007]),
    // so a reading that skipped monitor ports would find it silent.
    QList<PwPort> ports;
    PwNode virtualSource = node(u"rbtest.source.stereo"_s, u"Audio/Source/Virtual"_s);
    virtualSource.description = u"Test source"_s;
    PwPort left = port(virtualSource, PortDirection::Output, 0, u"FL"_s);
    left.isMonitor = true;
    PwPort right = port(virtualSource, PortDirection::Output, 1, u"FR"_s);
    right.isMonitor = true;
    ports.append(left);
    ports.append(right);

    const Inventory inventory = inventoryOf({virtualSource}, ports, {}, {});

    ASSERT_EQ(inventory.inputs.size(), 1);
    EXPECT_EQ(inventory.inputs.constFirst().channels, (QStringList{u"FL"_s, u"FR"_s}));
    EXPECT_TRUE(inventory.inputs.constFirst().isVirtual);
}

TEST(InventoryTest, AVirtualOutputCanBeRecordedFromItsMonitor)
{
    // Applications play into a virtual output, and a session records
    // what arrives there. A null sink does not say node.virtual; the factory
    // that made it is what gives it away.
    QList<PwPort> ports;
    PwNode everyApp = sink(u"virtual.everyapp"_s, u"Every app"_s, ports);
    everyApp.props.insert(u"factory.name"_s, u"support.null-audio-sink"_s);
    PwPort left = port(everyApp, PortDirection::Output, 0, u"FL"_s);
    left.isMonitor = true;
    PwPort right = port(everyApp, PortDirection::Output, 1, u"FR"_s);
    right.isMonitor = true;
    ports.append(left);
    ports.append(right);

    PwDefaults defaults;
    defaults.audioSink = u"virtual.everyapp"_s;
    const Inventory inventory = inventoryOf({everyApp}, ports, {}, defaults);

    ASSERT_EQ(inventory.outputs.size(), 1);
    EXPECT_TRUE(inventory.outputs.constFirst().isVirtual);
    EXPECT_FALSE(inventory.outputs.constFirst().isMonitor);
    ASSERT_EQ(inventory.inputs.size(), 1) << "the virtual output was not offered for recording";
    const InventoryDevice &monitor = inventory.inputs.constFirst();
    EXPECT_EQ(monitor.name, u"virtual.everyapp"_s);
    EXPECT_EQ(monitor.description, u"Every app"_s);
    EXPECT_TRUE(monitor.isMonitor);
    EXPECT_TRUE(monitor.isVirtual);
    EXPECT_EQ(monitor.channels, (QStringList{u"FL"_s, u"FR"_s}));
    // The default output's monitor is not the default input.
    EXPECT_FALSE(monitor.isDefault);
    EXPECT_TRUE(inventoryToJson(inventory).value("inputs"_L1).toArray().at(0).toObject().value("monitor"_L1).toBool());
}

TEST(InventoryTest, ADeviceReverblabrMadeIsVirtualByItsMarker)
{
    // ADR-0013: the devices the Virtual Devices window makes carry a marker
    // of their own, which is enough whatever else they say.
    QList<PwPort> ports;
    PwNode made = sink(u"reverblabr-virtual.0a1b2c3d"_s, u"Every app"_s, ports);
    made.props.insert(u"reverblabr.virtual-device"_s, u"user"_s);
    ports.append(port(made, PortDirection::Output, 0, u"FL"_s));
    ports.append(port(made, PortDirection::Output, 1, u"FR"_s));

    const Inventory inventory = inventoryOf({made}, ports, {}, {});

    ASSERT_EQ(inventory.outputs.size(), 1);
    EXPECT_TRUE(inventory.outputs.constFirst().isVirtual);
    ASSERT_EQ(inventory.inputs.size(), 1);
    EXPECT_TRUE(inventory.inputs.constFirst().isMonitor);
}

TEST(InventoryTest, AVirtualMicrophoneCanBePlayedTo)
{
    // A session hands what it made to an application through a
    // virtual microphone, by playing into the ports that feed it.
    QList<PwPort> ports;
    PwNode microphone = node(u"virtual.processed"_s, u"Audio/Source/Virtual"_s);
    microphone.description = u"Processed microphone"_s;
    ports.append(port(microphone, PortDirection::Input, 0, u"FL"_s));
    ports.append(port(microphone, PortDirection::Input, 1, u"FR"_s));
    ports.append(port(microphone, PortDirection::Output, 0, u"FL"_s));
    ports.append(port(microphone, PortDirection::Output, 1, u"FR"_s));

    const Inventory inventory = inventoryOf({microphone}, ports, {}, {});

    ASSERT_EQ(inventory.inputs.size(), 1);
    ASSERT_EQ(inventory.outputs.size(), 1) << "the virtual microphone was not offered to play to";
    EXPECT_EQ(inventory.outputs.constFirst().name, u"virtual.processed"_s);
    EXPECT_TRUE(inventory.outputs.constFirst().isVirtual);
    EXPECT_FALSE(inventory.outputs.constFirst().isMonitor);
    EXPECT_EQ(inventory.outputs.constFirst().channels, (QStringList{u"FL"_s, u"FR"_s}));
}

TEST(InventoryTest, TheDefaultsAreMarkedOnTheDeviceTheyName)
{
    QList<PwPort> ports;
    const PwNode speakers = sink(u"alsa.speakers"_s, u"Speakers"_s, ports);
    const PwNode headphones = sink(u"alsa.headphones"_s, u"Headphones"_s, ports);
    PwNode microphone = node(u"alsa.microphone"_s, u"Audio/Source"_s);
    microphone.description = u"Microphone"_s;

    PwDefaults defaults;
    defaults.audioSink = u"alsa.headphones"_s;
    defaults.audioSource = u"alsa.microphone"_s;
    // What the user chose is unplugged, so the two differ.
    defaults.configuredAudioSink = u"bluez.headset"_s;
    defaults.configuredAudioSource = u"alsa.microphone"_s;

    const Inventory inventory = inventoryOf({speakers, headphones, microphone}, ports, {}, defaults);

    ASSERT_EQ(inventory.outputs.size(), 2);
    // Sorted by what a person reads: Headphones before Speakers.
    EXPECT_EQ(inventory.outputs.at(0).name, u"alsa.headphones"_s);
    EXPECT_TRUE(inventory.outputs.at(0).isDefault);
    EXPECT_FALSE(inventory.outputs.at(1).isDefault);
    EXPECT_TRUE(inventory.inputs.constFirst().isDefault);
    EXPECT_EQ(inventory.defaultSink, u"alsa.headphones"_s);
    EXPECT_EQ(inventory.configuredSink, u"bluez.headset"_s);
}

TEST(InventoryTest, AnApplicationThatNamesNoIconGetsTheOneFromItsDesktopFile)
{
    QTemporaryDir data;
    ASSERT_TRUE(data.isValid());
    ASSERT_TRUE(QDir(data.path()).mkpath(u"applications"_s));
    QFile entry(data.filePath(u"applications/rbtest-inventory-player.desktop"_s));
    ASSERT_TRUE(entry.open(QIODevice::WriteOnly | QIODevice::Text));
    entry.write(
        "[Desktop Entry]\nName=Test Player\nIcon=rbtest-player-icon\n\n[Desktop Action New]\nIcon=the-wrong-one\n");
    entry.close();
    const QByteArray had = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", data.path().toUtf8());

    const PwNode fromDesktop = stream(u"a"_s, u"Test Player"_s, u"rbtest-inventory-player"_s);
    PwNode saysItself = stream(u"b"_s, u"Loud"_s, u"rbtest-inventory-loud"_s);
    saysItself.applicationIconName = u"an-icon-it-named"_s;

    const Inventory inventory = inventoryOf({fromDesktop, saysItself}, {}, {}, {});

    qputenv("XDG_DATA_HOME", had);
    EXPECT_EQ(appNamed(inventory, u"Test Player"_s)->iconName, u"rbtest-player-icon"_s);
    // What the stream says beats anything on disk.
    EXPECT_EQ(appNamed(inventory, u"Loud"_s)->iconName, u"an-icon-it-named"_s);
}

TEST(InventoryTest, ADesktopFileThatIsNotNamedAfterItsProgramIsFoundByWhatItRuns)
{
    // Fedora installs Firefox as org.mozilla.firefox.desktop, so a file
    // looked up by the program's name was never found.
    QTemporaryDir data;
    ASSERT_TRUE(data.isValid());
    ASSERT_TRUE(QDir(data.path()).mkpath(u"applications"_s));
    const auto write = [&](const QString &name, const QByteArray &contents) {
        QFile entry(data.filePath(u"applications/"_s + name));
        ASSERT_TRUE(entry.open(QIODevice::WriteOnly | QIODevice::Text));
        entry.write(contents);
    };
    write(u"org.example.browser.desktop"_s,
          "[Desktop Entry]\nName=Browser\nExec=/usr/bin/rbtest-browser %u\nIcon=rbtest-browser-icon\n\n"
          "[Desktop Action private]\nExec=rbtest-other --private\nIcon=the-wrong-one\n");
    write(u"org.example.wrapped.desktop"_s,
          "[Desktop Entry]\nName=Wrapped\nExec=env RBTEST=1 rbtest-wrapped\nIcon=rbtest-wrapped-icon\n");
    write(u"org.example.windowed.desktop"_s,
          "[Desktop Entry]\nName=Windowed\nExec=launcher --app\nStartupWMClass=rbtest-windowed\n"
          "Icon=rbtest-windowed-icon\n");
    const QByteArray had = qgetenv("XDG_DATA_HOME");
    qputenv("XDG_DATA_HOME", data.path().toUtf8());

    const PwNode browser = stream(u"a"_s, u"Browser"_s, u"rbtest-browser"_s);
    const PwNode wrapped = stream(u"b"_s, u"Wrapped"_s, u"rbtest-wrapped"_s);
    const PwNode windowed = stream(u"c"_s, u"Windowed"_s, u"rbtest-windowed"_s);
    // An action's command is not the application's.
    const PwNode other = stream(u"d"_s, u"Other"_s, u"rbtest-other"_s);

    const Inventory inventory = inventoryOf({browser, wrapped, windowed, other}, {}, {}, {});

    qputenv("XDG_DATA_HOME", had);
    EXPECT_EQ(appNamed(inventory, u"Browser"_s)->iconName, u"rbtest-browser-icon"_s);
    EXPECT_EQ(appNamed(inventory, u"Wrapped"_s)->iconName, u"rbtest-wrapped-icon"_s) << "env was not looked through";
    EXPECT_EQ(appNamed(inventory, u"Windowed"_s)->iconName, u"rbtest-windowed-icon"_s);
    EXPECT_TRUE(appNamed(inventory, u"Other"_s)->iconName.isEmpty()) << "an action's command was taken for the program";
}

TEST(InventoryTest, TheSameGraphAlwaysAddsUpToTheSameInventory)
{
    QList<PwPort> ports;
    const PwNode speakers = sink(u"alsa.speakers"_s, u"Speakers"_s, ports);
    const PwNode alsoSpeakers = sink(u"usb.speakers"_s, u"Speakers"_s, ports);
    const PwNode first = stream(u"a"_s, u"mpv"_s, u"mpv"_s);
    const PwNode second = stream(u"b"_s, u"Firefox"_s, u"firefox"_s);

    const Inventory forwards = inventoryOf({speakers, alsoSpeakers, first, second}, ports, {}, {});
    const Inventory backwards = inventoryOf({second, first, alsoSpeakers, speakers}, ports, {}, {});

    EXPECT_TRUE(forwards == backwards);
    // Two devices a person would call the same thing still hold their order.
    EXPECT_EQ(forwards.outputs.at(0).name, u"alsa.speakers"_s);
    EXPECT_EQ(forwards.outputs.at(1).name, u"usb.speakers"_s);
}

TEST(InventoryTest, TheJsonSaysWhatTheGuiReads)
{
    QList<PwPort> ports;
    const PwNode speakers = sink(u"alsa.speakers"_s, u"Speakers"_s, ports);
    const PwNode playing = stream(u"mpv.one"_s, u"mpv"_s, u"mpv"_s);
    PwDefaults defaults;
    defaults.audioSink = u"alsa.speakers"_s;

    const QJsonObject json =
        inventoryToJson(inventoryOf({speakers, playing}, ports, {link(playing, speakers)}, defaults));

    const QJsonObject application = json.value("applications"_L1).toArray().at(0).toObject();
    EXPECT_EQ(application.value("key"_L1).toString(), u"binary:mpv"_s);
    EXPECT_EQ(application.value("name"_L1).toString(), u"mpv"_s);
    EXPECT_EQ(application.value("streamCount"_L1).toInt(), 1);
    EXPECT_EQ(application.value("streams"_L1).toArray().at(0).toObject().value("kind"_L1).toString(), u"application"_s);

    const QJsonObject output = json.value("outputs"_L1).toArray().at(0).toObject();
    EXPECT_EQ(output.value("node"_L1).toString(), u"alsa.speakers"_s);
    EXPECT_EQ(output.value("channelCount"_L1).toInt(), 2);
    EXPECT_TRUE(output.value("default"_L1).toBool());
    EXPECT_EQ(json.value("defaults"_L1).toObject().value("sink"_L1).toString(), u"alsa.speakers"_s);
    EXPECT_TRUE(json.value("inputs"_L1).toArray().isEmpty());
    EXPECT_TRUE(json.value("otherStreams"_L1).toArray().isEmpty());
}

// A Bluetooth headset, in the two graphs written out in graphs/: the same
// device in its playback profile (A2DP) and in its headset profile
// (HSP/HFP), as WirePlumber 0.5 arranges it. The USB microphone beside it
// is the device that records normally.

TEST(InventoryTest, AHeadsetOnItsPlaybackProfileIsAnInputThatHearsNothing)
{
    // WirePlumber's loopback is there in both profiles, so the headset is
    // listed either way; in this one nothing stands behind it.
    const Inventory inventory = inventoryOfDump(u"bluez-headset-a2dp.json"_s);
    ASSERT_EQ(inventory.inputs.size(), 2) << "the headset or the USB microphone was not listed, or something else was";

    const InventoryDevice *headset = deviceNamed(inventory.inputs, u"bluez_input.00:1B:66:A1:B2:C3"_s);
    ASSERT_NE(headset, nullptr);
    EXPECT_EQ(headset->description, u"Wireless Headset"_s);
    EXPECT_EQ(headset->channels, QStringList{u"MONO"_s});
    EXPECT_TRUE(headset->isWireless);
    EXPECT_TRUE(headset->isOnPlaybackProfile);
    EXPECT_FALSE(headset->isVirtual);

    const InventoryDevice *usb = deviceNamed(inventory.inputs, u"alsa_input.usb-Microphone-00.mono-fallback"_s);
    ASSERT_NE(usb, nullptr);
    EXPECT_FALSE(usb->isWireless);
    EXPECT_FALSE(usb->isOnPlaybackProfile);

    // The headphones are wireless too, and the profile is nothing to them.
    const InventoryDevice *headphones = deviceNamed(inventory.outputs, u"bluez_output.00_1B_66_A1_B2_C3.1"_s);
    ASSERT_NE(headphones, nullptr);
    EXPECT_TRUE(headphones->isWireless);
    EXPECT_FALSE(headphones->isOnPlaybackProfile);
}

TEST(InventoryTest, AHeadsetOnItsHeadsetProfileRecordsNormally)
{
    const Inventory inventory = inventoryOfDump(u"bluez-headset-hfp.json"_s);

    const InventoryDevice *headset = deviceNamed(inventory.inputs, u"bluez_input.00:1B:66:A1:B2:C3"_s);
    ASSERT_NE(headset, nullptr);
    EXPECT_TRUE(headset->isWireless);
    EXPECT_FALSE(headset->isOnPlaybackProfile) << "a headset with its microphone there read as on its playback profile";
    // The microphone behind the loopback is WirePlumber's internal node, not
    // a second input to choose.
    EXPECT_EQ(deviceNamed(inventory.inputs, u"bluez_input.00_1B_66_A1_B2_C3.0"_s), nullptr);
    EXPECT_EQ(inventory.inputs.size(), 2);
}

TEST(InventoryTest, ADeviceIsWirelessByWhatItsDeviceSays)
{
    // A node that says nothing about Bluetooth itself is still wireless when
    // the device it belongs to does; a card's node is not.
    QList<PwPort> ports;
    PwNode speaker = sink(u"speaker"_s, u"Speaker"_s, ports);
    PwNode card = sink(u"card"_s, u"Card"_s, ports);
    PwDevice bluetooth;
    bluetooth.object.id = 900;
    bluetooth.props.insert(u"device.api"_s, u"bluez5"_s);
    PwDevice alsa;
    alsa.object.id = 901;
    alsa.props.insert(u"device.api"_s, u"alsa"_s);
    speaker.deviceId = bluetooth.object.id;
    card.deviceId = alsa.object.id;

    const Inventory inventory = inventoryOf({speaker, card}, ports, {}, {}, {bluetooth, alsa});

    EXPECT_TRUE(deviceNamed(inventory.outputs, u"speaker"_s)->isWireless);
    EXPECT_FALSE(deviceNamed(inventory.outputs, u"card"_s)->isWireless);
    // Without the devices there is nothing to tell by.
    EXPECT_FALSE(deviceNamed(inventoryOf({speaker}, ports, {}, {}).outputs, u"speaker"_s)->isWireless);
}

TEST(InventoryTest, TheJsonSaysWhichInputIsOnItsPlaybackProfile)
{
    const QJsonObject json = inventoryToJson(inventoryOfDump(u"bluez-headset-a2dp.json"_s));
    bool sawHeadset = false;
    for (const QJsonValue &entry : json.value("inputs"_L1).toArray()) {
        const QJsonObject input = entry.toObject();
        const bool headset = input.value("node"_L1).toString() == "bluez_input.00:1B:66:A1:B2:C3"_L1;
        sawHeadset = sawHeadset || headset;
        EXPECT_EQ(input.value("wireless"_L1).toBool(), headset);
        EXPECT_EQ(input.value("playbackProfile"_L1).toBool(), headset);
    }
    EXPECT_TRUE(sawHeadset);
}

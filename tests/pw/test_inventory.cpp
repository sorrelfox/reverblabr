// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The inventory against a graph PipeWire actually arranged: real
// applications starting and stopping, real devices loaded and unloaded, and
// the policy's own idea of the defaults. What the lists are made of is
// tests/unit/pw/test_inventory.cpp; what is measured here is that they are
// right about a live graph, and in time.

#include "pw/inventory.h"
#include "sessionharness.h"

#include <QElapsedTimer>
#include <QObject>
#include <gtest/gtest.h>

#include <optional>
#include <string>

using namespace rb;
using namespace rb::pw;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

/// The 250 ms allowed between the graph moving and the lists saying so.
constexpr qint64 kAllowed = 250;

/// Loads a null device the way a virtual-device applet would, and gives back
/// the module index to unload it with. Zero when pactl would not.
[[nodiscard]] int loadNullDevice(const std::string &nodeName, const std::string &description, bool asSource)
{
    // pactl's property list splits on spaces however the value is quoted,
    // so a description of more than one word has to arrive escaped.
    std::string escaped;
    for (const char letter : description) {
        if (letter == ' ') {
            escaped += '\\';
        }
        escaped += letter;
    }
    std::vector<std::string> argv{"pactl",
                                  "load-module",
                                  "module-null-sink",
                                  "sink_name=" + nodeName,
                                  "sink_properties=node.description='" + escaped + "'",
                                  "channel_map=front-left,front-right"};
    if (asSource) {
        argv.insert(argv.begin() + 3, "media.class=Audio/Source/Virtual");
    }
    const CommandResult result = runCommand(std::move(argv));
    if (result.status != 0) {
        return 0;
    }
    return std::atoi(result.output.c_str());
}

void unloadModule(int index)
{
    if (index > 0) {
        (void)runCommand({"pactl", "unload-module", std::to_string(index)});
    }
}

/// Unloads what a test loaded, however the test ends.
class NullDevice
{
public:
    NullDevice(const std::string &nodeName, const std::string &description, bool asSource)
        : m_index(loadNullDevice(nodeName, description, asSource))
    {
    }
    ~NullDevice()
    {
        unloadModule(m_index);
        m_index = 0;
    }
    NullDevice(const NullDevice &) = delete;
    NullDevice &operator=(const NullDevice &) = delete;

    [[nodiscard]] bool loaded() const
    {
        return m_index > 0;
    }
    /// Unloads it early, which is what a device going away looks like.
    void unload()
    {
        unloadModule(m_index);
        m_index = 0;
    }

private:
    int m_index = 0;
};

[[nodiscard]] const InventoryDevice *deviceNamed(const QList<InventoryDevice> &devices, const QString &name)
{
    for (const InventoryDevice &device : devices) {
        if (device.name == name) {
            return &device;
        }
    }
    return nullptr;
}

[[nodiscard]] const InventoryApp *appKeyed(const Inventory &inventory, const QString &key)
{
    for (const InventoryApp &application : inventory.applications) {
        if (application.key == key) {
            return &application;
        }
    }
    return nullptr;
}

/// Times the gap between the mirror hearing about a node and the inventory
/// saying so, which is what the criterion is about. Both ends are measured
/// inside this process, so a slow pw-dump on a busy machine cannot be
/// charged to the watcher.
class Stopwatch
{
public:
    Stopwatch(RegistryMirror &mirror, InventoryWatcher &watcher, std::function<bool(const Inventory &)> listed)
        : m_listed(std::move(listed))
    {
        QObject::connect(&mirror, &RegistryMirror::nodeAdded, &watcher, [this](const PwNode &) { started(); });
        QObject::connect(
            &mirror, &RegistryMirror::nodeRemoved, &watcher, [this](std::uint32_t, std::uint64_t) { started(); });
        QObject::connect(&watcher, &InventoryWatcher::changed, &watcher, [this](const Inventory &inventory) {
            if (m_elapsed < 0 && m_running && m_listed(inventory)) {
                m_elapsed = m_since.elapsed();
            }
        });
    }

    /// Starts the clock at the next thing the mirror hears.
    void arm()
    {
        m_elapsed = -1;
        m_running = false;
        m_arm = true;
    }

    [[nodiscard]] bool done() const
    {
        return m_elapsed >= 0;
    }

    [[nodiscard]] qint64 elapsed() const
    {
        return m_elapsed;
    }

private:
    void started()
    {
        if (m_arm) {
            m_arm = false;
            m_running = true;
            m_since.start();
        }
    }

    std::function<bool(const Inventory &)> m_listed;
    QElapsedTimer m_since;
    qint64 m_elapsed = -1;
    bool m_arm = false;
    bool m_running = false;
};

} // namespace

using Inventories = SessionTest;

TEST_F(Inventories, TheHarnessDevicesAreListedWithTheirChannels)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    InventoryWatcher watcher(engine.mirror());
    // **The defaults are the policy's to choose**, after the devices are
    // there: WirePlumber writes them into its metadata once it has looked
    // at every one. Read before that — as a busy machine let happen once
    // — the lists are right and the defaults empty. So the test
    // waits for the policy, as generously as anything else here.
    ASSERT_TRUE(engine.pumpUntil([&watcher] {
        watcher.refresh();
        const Inventory seen = watcher.inventory();
        return !seen.defaultSink.isEmpty() && !seen.defaultSource.isEmpty();
    })) << "the policy never chose a default sink and source";

    const Inventory inventory = watcher.inventory();
    const InventoryDevice *surround = deviceNamed(inventory.outputs, u"rbtest.sink.surround51"_s);
    ASSERT_NE(surround, nullptr) << "the harness's 5.1 sink is not in the outputs";
    EXPECT_EQ(surround->channels, (QStringList{u"FL"_s, u"FR"_s, u"FC"_s, u"LFE"_s, u"RL"_s, u"RR"_s}));
    EXPECT_EQ(surround->description, u"Test sink (5.1)"_s);

    const InventoryDevice *source = deviceNamed(inventory.inputs, u"rbtest.source.stereo"_s);
    ASSERT_NE(source, nullptr) << "the harness's stereo source is not in the inputs";
    // Its own output ports are flagged as monitors ([ADR-0007]); a reading
    // that skipped them would call this device silent.
    EXPECT_EQ(source->channels, (QStringList{u"FL"_s, u"FR"_s}));
    EXPECT_TRUE(source->isVirtual);

    EXPECT_FALSE(source->isMonitor);

    // **The harness's devices are all virtual**, null sinks every one, so a
    // sink is an input too, through its monitor, and a virtual source is an
    // output too, through the ports that feed it. Neither is a
    // default in its second list.
    const InventoryDevice *monitor = deviceNamed(inventory.inputs, u"rbtest.sink.stereo"_s);
    ASSERT_NE(monitor, nullptr) << "the virtual sink was not offered for recording";
    EXPECT_TRUE(monitor->isMonitor);
    EXPECT_FALSE(monitor->isDefault);
    EXPECT_EQ(monitor->channels, (QStringList{u"FL"_s, u"FR"_s}));
    const InventoryDevice *playedTo = deviceNamed(inventory.outputs, u"rbtest.source.stereo"_s);
    ASSERT_NE(playedTo, nullptr) << "the virtual source was not offered to play to";
    EXPECT_FALSE(playedTo->isDefault);
    EXPECT_EQ(playedTo->channels, (QStringList{u"FL"_s, u"FR"_s}));
    // The policy gave the highest-priority devices to the user.
    EXPECT_EQ(inventory.defaultSink, u"rbtest.sink.stereo"_s);
    EXPECT_EQ(inventory.defaultSource, u"rbtest.source.stereo"_s);
    EXPECT_TRUE(deviceNamed(inventory.outputs, u"rbtest.sink.stereo"_s)->isDefault);
}

TEST_F(Inventories, ADeviceComingAndGoingIsListedWithinTheQuarterSecond)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    InventoryWatcher watcher(engine.mirror());

    const QString sinkName = u"rbtest.dyn.sink"_s;
    Stopwatch appearing(engine.mirror(), watcher, [&sinkName](const Inventory &inventory) {
        return deviceNamed(inventory.outputs, sinkName) != nullptr;
    });
    appearing.arm();

    NullDevice sink("rbtest.dyn.sink", "Dynamic test sink", false);
    ASSERT_TRUE(sink.loaded()) << "pactl would not load a null sink";
    ASSERT_TRUE(engine.pumpUntil([&] { return appearing.done(); })) << "the sink never reached the outputs";
    EXPECT_LE(appearing.elapsed(), kAllowed) << "the sink took " << appearing.elapsed() << " ms to be listed";
    EXPECT_EQ(deviceNamed(watcher.inventory().outputs, sinkName)->description, u"Dynamic test sink"_s);

    Stopwatch leaving(engine.mirror(), watcher, [&sinkName](const Inventory &inventory) {
        return deviceNamed(inventory.outputs, sinkName) == nullptr;
    });
    leaving.arm();
    sink.unload();
    ASSERT_TRUE(engine.pumpUntil([&] { return leaving.done(); })) << "the sink never left the outputs";
    EXPECT_LE(leaving.elapsed(), kAllowed) << "the sink took " << leaving.elapsed() << " ms to go";
}

TEST_F(Inventories, ASourceComingAndGoingIsListedWithinTheQuarterSecond)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    InventoryWatcher watcher(engine.mirror());

    const QString sourceName = u"rbtest.dyn.source"_s;
    Stopwatch appearing(engine.mirror(), watcher, [&sourceName](const Inventory &inventory) {
        return deviceNamed(inventory.inputs, sourceName) != nullptr;
    });
    appearing.arm();

    NullDevice source("rbtest.dyn.source", "Dynamic test source", true);
    ASSERT_TRUE(source.loaded()) << "pactl would not load a null source";
    ASSERT_TRUE(engine.pumpUntil([&] { return appearing.done(); })) << "the source never reached the inputs";
    EXPECT_LE(appearing.elapsed(), kAllowed) << "the source took " << appearing.elapsed() << " ms to be listed";
    EXPECT_EQ(deviceNamed(watcher.inventory().inputs, sourceName)->channels, (QStringList{u"FL"_s, u"FR"_s}));
}

TEST_F(Inventories, AnApplicationIsListedWithinTheQuarterSecondOfStarting)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    InventoryWatcher watcher(engine.mirror());

    const QString key = u"binary:quickapp"_s;
    Stopwatch appearing(
        engine.mirror(), watcher, [&key](const Inventory &inventory) { return appKeyed(inventory, key) != nullptr; });
    appearing.arm();

    FakeApp app(appOptions("QuickApp", "quickapp"));
    ASSERT_TRUE(engine.pumpUntil([&] { return appearing.done(); })) << "the application never reached the list";
    EXPECT_LE(appearing.elapsed(), kAllowed) << "the application took " << appearing.elapsed() << " ms to be listed";

    const InventoryApp *listed = appKeyed(watcher.inventory(), key);
    ASSERT_NE(listed, nullptr);
    EXPECT_EQ(listed->displayName, u"QuickApp"_s);
    EXPECT_EQ(listed->binary, u"quickapp"_s);
    EXPECT_EQ(listed->streamCount(), 1);

    Stopwatch leaving(
        engine.mirror(), watcher, [&key](const Inventory &inventory) { return appKeyed(inventory, key) == nullptr; });
    leaving.arm();
    app.stop();
    ASSERT_TRUE(engine.pumpUntil([&] { return leaving.done(); })) << "the application never left the list";
    EXPECT_LE(leaving.elapsed(), kAllowed) << "the application took " << leaving.elapsed() << " ms to go";
}

TEST_F(Inventories, TwoStreamsOfOneApplicationAreOneApplication)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    InventoryWatcher watcher(engine.mirror());

    FakeApp first(appOptions("TwiceApp", "twiceapp"));
    FakeApp second(appOptions("TwiceApp", "twiceapp"));

    const QString key = u"binary:twiceapp"_s;
    ASSERT_TRUE(engine.pumpUntil([&] {
        const InventoryApp *application = appKeyed(watcher.inventory(), key);
        return application != nullptr && application->streamCount() == 2;
    })) << "the two streams never became one application of two";

    const InventoryApp *application = appKeyed(watcher.inventory(), key);
    ASSERT_NE(application, nullptr);
    EXPECT_EQ(application->streamCount(), 2);
    // Two streams, one sink, named once.
    EXPECT_EQ(application->sinks, QStringList{QString::fromLatin1(kStereoSink)});
    // Both streams are called `pw-cat` — a stream's node.name is not an
    // identifier. The serial is what tells them apart.
    EXPECT_NE(application->streams.at(0).serial, application->streams.at(1).serial);

    // One of them stopping leaves the application there with one stream.
    second.stop();
    ASSERT_TRUE(engine.pumpUntil([&] {
        const InventoryApp *left = appKeyed(watcher.inventory(), key);
        return left != nullptr && left->streamCount() == 1;
    })) << "the application did not come back down to one stream";
}

TEST_F(Inventories, OurOwnSessionNodeIsInNoList)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    InventoryWatcher watcher(engine.mirror());

    TestSession session;
    SessionNode node(engine.connection());
    ASSERT_TRUE(node.start(TestSession::spec(u"33333333-4444"_s, u"Inventory"_s)).has_value());
    const QString ourName = node.nodeName();
    ASSERT_TRUE(engine.awaitNode(ourName).has_value()) << "our own node never reached the graph";
    ASSERT_TRUE(engine.pumpUntil([&] { return engine.mirror().nodeNamed(ourName).has_value(); }));
    watcher.refresh();

    const Inventory inventory = watcher.inventory();
    EXPECT_EQ(deviceNamed(inventory.inputs, ourName), nullptr);
    EXPECT_EQ(deviceNamed(inventory.outputs, ourName), nullptr);
    for (const InventoryApp &application : inventory.applications) {
        EXPECT_FALSE(application.displayName.contains(u"Reverblabr"_s)) << "we offered ourselves as an application";
        for (const InventoryStream &stream : application.streams) {
            EXPECT_NE(stream.name, ourName);
        }
    }
    for (const InventoryStream &stream : inventory.otherStreams) {
        EXPECT_NE(stream.name, ourName);
    }
    // The mirror does know about it; it is the inventory that leaves it out.
    EXPECT_TRUE(engine.mirror().nodeNamed(ourName).has_value());
}

TEST_F(Inventories, TheDefaultMovesWhenThePolicySaysSo)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    InventoryWatcher watcher(engine.mirror());
    watcher.refresh();
    ASSERT_EQ(watcher.inventory().defaultSink, QString::fromLatin1(kStereoSink));

    const std::optional<PwNode> mono = engine.mirror().nodeNamed(QString::fromLatin1(kMonoSink));
    ASSERT_TRUE(mono.has_value());
    const std::optional<PwNode> stereo = engine.mirror().nodeNamed(QString::fromLatin1(kStereoSink));
    ASSERT_TRUE(stereo.has_value());

    (void)runCommand({"wpctl", "set-default", std::to_string(mono->object.id)});
    ASSERT_TRUE(engine.pumpUntil([&] { return watcher.inventory().defaultSink == QString::fromLatin1(kMonoSink); }))
        << "the inventory never followed the default to the mono sink";

    const Inventory moved = watcher.inventory();
    EXPECT_TRUE(deviceNamed(moved.outputs, QString::fromLatin1(kMonoSink))->isDefault);
    EXPECT_FALSE(deviceNamed(moved.outputs, QString::fromLatin1(kStereoSink))->isDefault);

    (void)runCommand({"wpctl", "set-default", std::to_string(stereo->object.id)});
    ASSERT_TRUE(engine.pumpUntil([&] { return watcher.inventory().defaultSink == QString::fromLatin1(kStereoSink); }))
        << "the default never came back";
}

TEST_F(Inventories, ABurstOfChangesIsOneInventory)
{
    Pump engine;
    ASSERT_TRUE(engine.waitUntilReady());
    InventoryWatcher watcher(engine.mirror());

    int mirrorEvents = 0;
    const auto count = [&mirrorEvents] { ++mirrorEvents; };
    QObject::connect(&engine.mirror(), &RegistryMirror::nodeAdded, &watcher, count);
    QObject::connect(&engine.mirror(), &RegistryMirror::portAdded, &watcher, count);
    QObject::connect(&engine.mirror(), &RegistryMirror::linkAdded, &watcher, count);
    int inventories = 0;
    QObject::connect(
        &watcher, &InventoryWatcher::changed, &watcher, [&inventories](const Inventory &) { ++inventories; });

    // Three devices at once: each is a node and a handful of ports, and
    // every one of them would otherwise be an inventory sent to the GUI.
    NullDevice first("rbtest.burst.one", "Burst one", false);
    NullDevice second("rbtest.burst.two", "Burst two", false);
    NullDevice third("rbtest.burst.three", "Burst three", true);
    ASSERT_TRUE(first.loaded() && second.loaded() && third.loaded());

    ASSERT_TRUE(engine.pumpUntil([&] {
        const Inventory inventory = watcher.inventory();
        return deviceNamed(inventory.outputs, u"rbtest.burst.one"_s) != nullptr
            && deviceNamed(inventory.outputs, u"rbtest.burst.two"_s) != nullptr
            && deviceNamed(inventory.inputs, u"rbtest.burst.three"_s) != nullptr;
    })) << "the three devices never all arrived";
    engine.pump(std::chrono::milliseconds{300}); // let anything still settling settle

    EXPECT_GE(mirrorEvents, 9) << "three null devices should be at least three nodes and six ports";
    EXPECT_LT(inventories, mirrorEvents) << "every change of the graph became an inventory of its own";
}

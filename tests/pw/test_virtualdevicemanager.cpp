// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The virtual device manager (ADR-0013): the files and the live graph
// kept in step, in the private instance, judged by the registry and by
// pw-dump.

#include "pw/connection.h"
#include "pw/inventory.h"
#include "pw/pwobjects.h"
#include "pw/registrymirror.h"
#include "pw/virtualdeviceconf.h"
#include "pw/virtualdevicemanager.h"
#include "pwtest.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>

using namespace rb::pw;
using namespace rb::pwtest;
using namespace Qt::StringLiterals;

namespace
{

/// Lets Qt, and so PipeWire, run until \a condition holds or the time runs
/// out. Returns whether it held.
[[nodiscard]] bool pumpUntil(const std::function<bool()> &condition,
                             std::chrono::milliseconds timeout = kDefaultTimeout)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeout.count()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

void pumpFor(std::chrono::milliseconds duration)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < duration.count()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

/// A connection, a mirror and a manager, reading two files of the test's.
class Engine
{
public:
    explicit Engine(const QTemporaryDir &files)
        : m_manager(m_connection, m_mirror)
    {
        m_manager.setStores(VirtualDeviceStore(VirtualDeviceScope::User, userFile(files)),
                            VirtualDeviceStore(VirtualDeviceScope::System, systemFile(files)));
        m_connection.setRetryDelays(std::chrono::milliseconds{50}, std::chrono::milliseconds{200});
        m_mirror.setOwnApplicationName(u"reverblabr-virtual-device-test"_s);
        m_connection.start();
    }

    static QString userFile(const QTemporaryDir &files)
    {
        return files.filePath(u"user/90-reverblabr-virtual-devices.conf"_s);
    }

    static QString systemFile(const QTemporaryDir &files)
    {
        return files.filePath(u"system/90-reverblabr-virtual-devices-system.conf"_s);
    }

    Connection &connection()
    {
        return m_connection;
    }

    RegistryMirror &mirror()
    {
        return m_mirror;
    }

    VirtualDeviceManager &manager()
    {
        return m_manager;
    }

    [[nodiscard]] bool waitUntilReady()
    {
        return pumpUntil([this] { return m_mirror.isReady(); });
    }

    [[nodiscard]] QList<PwNode> nodesNamed(const QString &name) const
    {
        QList<PwNode> found;
        for (const PwNode &node : m_mirror.nodes()) {
            if (node.name == name) {
                found.append(node);
            }
        }
        return found;
    }

    [[nodiscard]] std::optional<VirtualDeviceStatus> status(const QString &id) const
    {
        const std::optional<VirtualDeviceState> state = m_manager.device(id);
        return state ? std::optional{state->status} : std::nullopt;
    }

    /// Waits until the device's node is there with its ports.
    [[nodiscard]] bool waitForDevice(const QString &id, const QString &description = {})
    {
        const QString name = virtualDeviceNodeName(id);
        return pumpUntil([&] {
            const QList<PwNode> nodes = nodesNamed(name);
            return nodes.size() == 1 && (description.isEmpty() || nodes.first().description == description)
                && !m_mirror.portsOf(nodes.first().object.id, PortDirection::Input).isEmpty()
                && status(id) == VirtualDeviceStatus::Present;
        });
    }

    [[nodiscard]] bool waitForNoNode(const QString &id)
    {
        const QString name = virtualDeviceNodeName(id);
        return pumpUntil([&] { return nodesNamed(name).isEmpty(); });
    }

private:
    Connection m_connection;
    RegistryMirror m_mirror{m_connection};
    VirtualDeviceManager m_manager;
};

/// How many of \a name's nodes pw-dump sees, from outside our connection.
[[nodiscard]] std::size_t nodesInGraph(const QString &name)
{
    const Graph graph = Graph::capture();
    std::size_t count = 0;
    for (const Graph::Node &node : graph.nodes) {
        if (node.prop("node.name") == name.toStdString()) {
            ++count;
        }
    }
    return count;
}

int argc = 1;
char programName[] = "reverblabr-virtual-device-test";
char *argv[] = {programName, nullptr};

} // namespace

class VirtualDeviceManagers : public PipeWireTest
{
protected:
    void SetUp() override
    {
        PipeWireTest::SetUp();
        m_app = std::make_unique<QCoreApplication>(argc, argv);
        ASSERT_TRUE(m_files.isValid());
    }

    void TearDown() override
    {
        m_app.reset();
        PipeWireTest::TearDown();
    }

    QTemporaryDir m_files;

private:
    std::unique_ptr<QCoreApplication> m_app;
};

TEST_F(VirtualDeviceManagers, ANewDeviceIsInTheFileAndInTheGraphUnderItsName)
{
    Engine engine(m_files);
    ASSERT_TRUE(engine.waitUntilReady());

    const auto output = engine.manager().create(
        u"  Everything I hear "_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Stereo);
    ASSERT_TRUE(output.has_value()) << virtualDeviceErrorName(output.error());
    const auto microphone =
        engine.manager().create(u"Clean voice"_s, VirtualDeviceDirection::Input, VirtualDeviceChannels::Mono);
    ASSERT_TRUE(microphone.has_value());
    EXPECT_EQ(output->name, u"Everything I hear"_s);

    // The file first: it is what PipeWire makes them from at its next start.
    const auto onDisk = engine.manager().store(VirtualDeviceScope::User).load();
    ASSERT_TRUE(onDisk.has_value());
    EXPECT_EQ(onDisk->size(), 2);

    ASSERT_TRUE(engine.waitForDevice(output->id, u"Everything I hear"_s));
    ASSERT_TRUE(engine.waitForDevice(microphone->id, u"Clean voice"_s));

    const PwNode sink = engine.nodesNamed(virtualDeviceNodeName(output->id)).first();
    EXPECT_EQ(sink.mediaClass, u"Audio/Sink"_s);
    EXPECT_EQ(sink.props.value(u"reverblabr.virtual-device"_s), u"user"_s);
    EXPECT_TRUE(sink.isVirtual);
    EXPECT_FALSE(sink.isOurs) << "a virtual device is a device, not one of our processing nodes";
    const PwNode mic = engine.nodesNamed(virtualDeviceNodeName(microphone->id)).first();
    EXPECT_EQ(mic.mediaClass, u"Audio/Source/Virtual"_s);
    EXPECT_EQ(engine.mirror().portsOf(mic.object.id, PortDirection::Input).size(), 1);

    // **The lists show them by the user's name**: the output as something a
    // session records, the microphone as something it plays into.
    const Inventory inventory = inventoryOf(
        engine.mirror().nodes(), engine.mirror().ports(), engine.mirror().links(), engine.mirror().defaults());
    const auto listed = [](const QList<InventoryDevice> &devices, const QString &name) {
        return std::ranges::find_if(devices, [&](const InventoryDevice &device) { return device.name == name; });
    };
    const auto input = listed(inventory.inputs, sink.name);
    ASSERT_NE(input, inventory.inputs.end()) << "a virtual output is recorded from its monitor";
    EXPECT_EQ(input->description, u"Everything I hear"_s);
    EXPECT_TRUE(input->isVirtual);
    const auto playedInto = listed(inventory.outputs, mic.name);
    ASSERT_NE(playedInto, inventory.outputs.end()) << "a virtual microphone is something a session plays into";
    EXPECT_EQ(playedInto->description, u"Clean voice"_s);
}

TEST_F(VirtualDeviceManagers, ARenameKeepsTheNodeNameAndNeverSendsItsStreamsToTheDefaultDevice)
{
    Engine engine(m_files);
    ASSERT_TRUE(engine.waitUntilReady());
    const auto device =
        engine.manager().create(u"Old name"_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Stereo);
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(engine.waitForDevice(device->id));
    const QString name = virtualDeviceNodeName(device->id);

    // An application that plays into the device by name, the way one that
    // was told to use it does.
    FakeAppOptions options;
    options.applicationName = "RenameWatcher";
    options.binary = "renamewatcher";
    options.target = name.toStdString();
    FakeApp app(options);
    std::optional<PwNode> stream;
    ASSERT_TRUE(pumpUntil([&] {
        for (const PwNode &node : engine.mirror().nodes()) {
            if (node.applicationName == u"RenameWatcher"_s && node.kind == NodeKind::PlaybackStream) {
                const QList<PwNode> device = engine.nodesNamed(name);
                if (!device.isEmpty() && engine.mirror().isLinked(node.object.id, device.first().object.id)) {
                    stream = node;
                    return true;
                }
            }
        }
        return false;
    }));
    const std::optional<PwNode> defaultSink = engine.mirror().defaultSink();
    ASSERT_TRUE(defaultSink.has_value());
    ASSERT_NE(defaultSink->name, name);

    // Every link the stream is given to the default device while the rename
    // runs — however brief — is announced on the registry, so counting the
    // announcements misses none of them.
    int fallbackLinks = 0;
    QObject::connect(&engine.mirror(), &RegistryMirror::linkAdded, [&](const PwLink &link) {
        if (link.outputNode == stream->object.id && link.inputNode == defaultSink->object.id) {
            ++fallbackLinks;
        }
    });

    ASSERT_TRUE(engine.manager().rename(device->id, u"New name"_s).has_value());
    ASSERT_TRUE(engine.waitForDevice(device->id, u"New name"_s));
    ASSERT_TRUE(pumpUntil([&] {
        const QList<PwNode> nodes = engine.nodesNamed(name);
        return nodes.size() == 1 && engine.mirror().isLinked(stream->object.id, nodes.first().object.id);
    })) << "the stream follows the device to its new node";
    pumpFor(std::chrono::milliseconds{200});

    EXPECT_EQ(fallbackLinks, 0) << "the stream was sent to the default device during the rename";
    EXPECT_EQ(engine.nodesNamed(name).size(), 1);
    const auto onDisk = engine.manager().store(VirtualDeviceScope::User).load();
    ASSERT_TRUE(onDisk.has_value());
    EXPECT_EQ(onDisk->first().name, u"New name"_s);
    EXPECT_EQ(onDisk->first().id, device->id);
}

TEST_F(VirtualDeviceManagers, DisablingAndDeletingTakeTheNodeAwayAndEnablingBringsItBack)
{
    Engine engine(m_files);
    ASSERT_TRUE(engine.waitUntilReady());
    const auto device =
        engine.manager().create(u"Switchable"_s, VirtualDeviceDirection::Input, VirtualDeviceChannels::Stereo);
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(engine.waitForDevice(device->id));

    ASSERT_TRUE(engine.manager().setEnabled(device->id, false).has_value());
    EXPECT_EQ(engine.status(device->id), VirtualDeviceStatus::Disabled);
    ASSERT_TRUE(engine.waitForNoNode(device->id));
    const auto disabled = engine.manager().store(VirtualDeviceScope::User).load();
    ASSERT_TRUE(disabled.has_value());
    ASSERT_EQ(disabled->size(), 1) << "a disabled device stays in its file";
    EXPECT_FALSE(disabled->first().enabled);

    ASSERT_TRUE(engine.manager().setEnabled(device->id, true).has_value());
    ASSERT_TRUE(engine.waitForDevice(device->id, u"Switchable"_s));

    ASSERT_TRUE(engine.manager().remove(device->id).has_value());
    ASSERT_TRUE(engine.waitForNoNode(device->id));
    EXPECT_FALSE(engine.manager().device(device->id).has_value());
    EXPECT_FALSE(QFile::exists(Engine::userFile(m_files))) << "no devices, no file";
    EXPECT_EQ(engine.manager().setEnabled(device->id, true).error(), VirtualDeviceError::NotFound);
}

TEST_F(VirtualDeviceManagers, ADeviceOutlivesTheAppAndTheNextStartMakesNoSecondOne)
{
    QString id;
    {
        Engine engine(m_files);
        ASSERT_TRUE(engine.waitUntilReady());
        const auto device =
            engine.manager().create(u"Lasting"_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Mono);
        ASSERT_TRUE(device.has_value());
        ASSERT_TRUE(engine.waitForDevice(device->id));
        id = device->id;
    } // the manager, the mirror and the connection go, as they do when the app quits

    const QString name = virtualDeviceNodeName(id);
    ASSERT_TRUE(waitUntil([&](const Graph &graph) {
        return graph.findNode([&](const Graph::Node &node) { return node.prop("node.name") == name.toStdString(); })
            != nullptr;
    }));
    pumpFor(std::chrono::milliseconds{300});
    EXPECT_EQ(nodesInGraph(name), 1u) << "the node lingers after its creator has gone";

    Engine again(m_files);
    // **Not even for a moment.** A second node would be tidied away as a
    // duplicate soon after, so counting the nodes at the end is not enough;
    // every node of that name the registry announces is counted instead.
    QList<std::uint64_t> serials;
    QObject::connect(&again.mirror(), &RegistryMirror::nodeAdded, [&](const PwNode &node) {
        if (node.name == name && !serials.contains(node.object.serial)) {
            serials.append(node.object.serial);
        }
    });
    ASSERT_TRUE(again.waitUntilReady());
    ASSERT_TRUE(pumpUntil([&] { return again.status(id) == VirtualDeviceStatus::Present; }));
    pumpFor(std::chrono::milliseconds{300});
    EXPECT_EQ(again.nodesNamed(name).size(), 1) << "a second start must not make a second node";
    EXPECT_EQ(serials.size(), 1) << "a second node of that name was made, however briefly";
    EXPECT_EQ(nodesInGraph(name), 1u);
}

TEST_F(VirtualDeviceManagers, AStartMakesTheEnabledDevicesTheFileHoldsAndNotTheDisabledOnes)
{
    VirtualDevice on{u"a1b2c3d4"_s,
                     u"From the file"_s,
                     VirtualDeviceDirection::Output,
                     VirtualDeviceChannels::Stereo,
                     true,
                     VirtualDeviceScope::User};
    VirtualDevice off = on;
    off.id = u"a1b2c3d5"_s;
    off.name = u"Switched off"_s;
    off.enabled = false;
    VirtualDevice everyone = on;
    everyone.id = u"a1b2c3d6"_s;
    everyone.name = u"Everyone's"_s;
    everyone.direction = VirtualDeviceDirection::Input;
    everyone.scope = VirtualDeviceScope::System;
    ASSERT_TRUE(VirtualDeviceStore(VirtualDeviceScope::User, Engine::userFile(m_files)).save({on, off}));
    ASSERT_TRUE(VirtualDeviceStore(VirtualDeviceScope::System, Engine::systemFile(m_files)).save({everyone}));

    Engine engine(m_files);
    EXPECT_EQ(engine.status(on.id), VirtualDeviceStatus::WaitingForPipeWire);
    ASSERT_TRUE(engine.waitUntilReady());
    ASSERT_TRUE(engine.waitForDevice(on.id, u"From the file"_s));
    ASSERT_TRUE(engine.waitForDevice(everyone.id, u"Everyone's"_s));
    EXPECT_EQ(engine.nodesNamed(virtualDeviceNodeName(everyone.id)).first().props.value(u"reverblabr.virtual-device"_s),
              u"system"_s);
    pumpFor(std::chrono::milliseconds{200});
    EXPECT_TRUE(engine.nodesNamed(virtualDeviceNodeName(off.id)).isEmpty());
    EXPECT_EQ(engine.status(off.id), VirtualDeviceStatus::Disabled);

    // **All users' devices are the helper's to change**, not ours.
    EXPECT_EQ(engine.manager().rename(everyone.id, u"Mine now"_s).error(), VirtualDeviceError::ReadOnlyScope);
    EXPECT_EQ(engine.manager().remove(everyone.id).error(), VirtualDeviceError::ReadOnlyScope);
}

TEST_F(VirtualDeviceManagers, AMarkedNodeNoFileAccountsForGoesUnlessItsFileCannotBeRead)
{
    // A node of ours that no file holds, left behind the way a file edited
    // by hand while PipeWire ran leaves one.
    const auto makeStray = [](const std::string &id, const std::string &scope) {
        const CommandResult made = runCommand({"pw-cli",
                                               "create-node",
                                               "adapter",
                                               "{ factory.name = support.null-audio-sink node.name = "
                                               "reverblabr-virtual."
                                                   + id
                                                   + " media.class = Audio/Sink object.linger = true "
                                                     "reverblabr.virtual-device = "
                                                   + scope + " }"});
        ASSERT_EQ(made.status, 0) << made.output;
    };
    makeStray("0badf00d", "user");
    makeStray("0badf00e", "system");
    ASSERT_TRUE(waitForNode(nodeNamed("reverblabr-virtual.0badf00d")).has_value());
    ASSERT_TRUE(waitForNode(nodeNamed("reverblabr-virtual.0badf00e")).has_value());

    // The system file is somebody else's: nothing of that scope is touched.
    QDir().mkpath(QFileInfo(Engine::systemFile(m_files)).path());
    QFile foreign(Engine::systemFile(m_files));
    ASSERT_TRUE(foreign.open(QIODevice::WriteOnly));
    foreign.write("context.objects = [ ]\n");
    foreign.close();

    Engine engine(m_files);
    EXPECT_EQ(engine.manager().scopeError(VirtualDeviceScope::System), VirtualDeviceError::NotOurs);
    ASSERT_TRUE(engine.waitUntilReady());
    ASSERT_TRUE(engine.waitForNoNode(u"0badf00d"_s)) << "a stray node of the user's scope is removed";
    pumpFor(std::chrono::milliseconds{300});
    EXPECT_EQ(engine.nodesNamed(u"reverblabr-virtual.0badf00e"_s).size(), 1)
        << "a scope whose file cannot be read is left alone";
}

TEST_F(VirtualDeviceManagers, ADeviceSomethingElseRemovedIsMissingUntilTheNextReload)
{
    Engine engine(m_files);
    ASSERT_TRUE(engine.waitUntilReady());
    const auto device =
        engine.manager().create(u"Fragile"_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Stereo);
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(engine.waitForDevice(device->id));

    const PwNode node = engine.nodesNamed(virtualDeviceNodeName(device->id)).first();
    const CommandResult destroyed = runCommand({"pw-cli", "destroy", std::to_string(node.object.id)});
    ASSERT_EQ(destroyed.status, 0) << destroyed.output;
    ASSERT_TRUE(engine.waitForNoNode(device->id));
    pumpFor(std::chrono::milliseconds{300});
    EXPECT_EQ(engine.status(device->id), VirtualDeviceStatus::Missing)
        << "a node somebody else removed is not made again behind their back";

    engine.manager().reload();
    ASSERT_TRUE(engine.waitForDevice(device->id, u"Fragile"_s));
}

TEST_F(VirtualDeviceManagers, AReconnectFindsTheDeviceAndMakesNoSecondOne)
{
    Engine engine(m_files);
    ASSERT_TRUE(engine.waitUntilReady());
    const auto device =
        engine.manager().create(u"Steady"_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Stereo);
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(engine.waitForDevice(device->id));
    const QString name = virtualDeviceNodeName(device->id);
    const std::uint64_t serial = engine.nodesNamed(name).first().object.serial;

    // The mirror empties and fills again, and the manager reconciles from
    // scratch against what is there.
    engine.connection().stop();
    ASSERT_TRUE(pumpUntil([&] { return !engine.mirror().isReady(); }));
    EXPECT_EQ(engine.status(device->id), VirtualDeviceStatus::WaitingForPipeWire);
    engine.connection().start();
    ASSERT_TRUE(engine.waitUntilReady());
    ASSERT_TRUE(engine.waitForDevice(device->id));
    pumpFor(std::chrono::milliseconds{300});
    ASSERT_EQ(engine.nodesNamed(name).size(), 1);
    EXPECT_EQ(engine.nodesNamed(name).first().object.serial, serial) << "the same node, not a new one";
}

TEST_F(VirtualDeviceManagers, TwoRenamesInARowEndOnTheSecondName)
{
    Engine engine(m_files);
    ASSERT_TRUE(engine.waitUntilReady());
    const auto device =
        engine.manager().create(u"First"_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Stereo);
    ASSERT_TRUE(device.has_value());
    ASSERT_TRUE(engine.waitForDevice(device->id));

    // The second arrives while the first rename's node is still on its way.
    ASSERT_TRUE(engine.manager().rename(device->id, u"Second"_s).has_value());
    QCoreApplication::processEvents();
    ASSERT_TRUE(engine.manager().rename(device->id, u"Third"_s).has_value());
    EXPECT_TRUE(engine.waitForDevice(device->id, u"Third"_s));
}

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Virtual devices against a real PipeWire (ADR-0013). What the drop-in
// says is tests/unit/pw/test_virtualdeviceconf.cpp; here a PipeWire daemon
// reads it, the way the user's does at login.
//
// The harness's own daemon has already read its configuration, so each test
// starts a second, nested one on a socket of its own, with the distribution's
// pipewire.conf and nothing in pipewire.conf.d but the file under test. It
// needs no session manager: a daemon creates the objects in its
// configuration by itself.

#include "pw/virtualdeviceconf.h"
#include "pwtest.h"

#include <QDir>
#include <QFile>
#include <QString>
#include <gtest/gtest.h>

#include <cstdlib>
#include <memory>
#include <string>

using namespace Qt::StringLiterals;
using namespace rb::pw;
using namespace rb::pwtest;

namespace
{

constexpr const char *kNestedCore = "rbtest-virtual-devices";

class VirtualDeviceFiles : public PipeWireTest
{
protected:
    void SetUp() override
    {
        PipeWireTest::SetUp();
        const char *harness = std::getenv("RB_PW_HARNESS_DIR");
        ASSERT_NE(harness, nullptr);
        m_configDir = QString::fromLocal8Bit(harness) + u"/nested-"_s
            + QString::fromStdString(::testing::UnitTest::GetInstance()->current_test_info()->name());
        ASSERT_TRUE(QDir().mkpath(m_configDir + u"/pipewire.conf.d"_s));
        QFile::remove(m_configDir + u"/pipewire.conf"_s);
        ASSERT_TRUE(QFile::copy(u"/usr/share/pipewire/pipewire.conf"_s, m_configDir + u"/pipewire.conf"_s));
    }

    void TearDown() override
    {
        m_daemon.reset();
        ::unsetenv("PIPEWIRE_REMOTE");
        PipeWireTest::TearDown();
    }

    /// Where the nested daemon looks for drop-ins, as `$XDG_CONFIG_HOME`'s
    /// would be for the user's own.
    [[nodiscard]] QString dropIn(const QString &fileName) const
    {
        return m_configDir + u"/pipewire.conf.d/"_s + fileName;
    }

    /// Starts the nested daemon and points every PipeWire tool this test runs
    /// at it.
    void startDaemon()
    {
        // Only the daemon gets PIPEWIRE_CONFIG_DIR: clients would look for
        // client.conf there.
        m_daemon = std::make_unique<BackgroundProcess>(
            std::vector<std::string>{"env",
                                     "PIPEWIRE_CONFIG_DIR=" + m_configDir.toStdString(),
                                     std::string("PIPEWIRE_CORE=") + kNestedCore,
                                     "pipewire"},
            "nested-pipewire.log");
        ::setenv("PIPEWIRE_REMOTE", kNestedCore, 1);
    }

private:
    QString m_configDir;
    std::unique_ptr<BackgroundProcess> m_daemon;
};

[[nodiscard]] VirtualDevice device(const QString &id, const QString &name, VirtualDeviceScope scope)
{
    VirtualDevice made;
    made.id = id;
    made.name = name;
    made.scope = scope;
    return made;
}

} // namespace

// The user's file: every enabled device appears under its name, with its
// class and its marker, and the disabled one does not.
TEST_F(VirtualDeviceFiles, PipeWireMakesTheEnabledDevicesAndNotTheDisabledOne)
{
    VirtualDevice output = device(u"0000000a"_s, u"Everything \"else\" — ünï"_s, VirtualDeviceScope::User);
    VirtualDevice microphone = device(u"0000000b"_s, u"Processed mic"_s, VirtualDeviceScope::User);
    microphone.direction = VirtualDeviceDirection::Input;
    microphone.channels = VirtualDeviceChannels::Mono;
    VirtualDevice disabled = device(u"0000000c"_s, u"Switched off"_s, VirtualDeviceScope::User);
    disabled.enabled = false;

    const VirtualDeviceStore store(VirtualDeviceScope::User, dropIn(u"90-reverblabr-virtual-devices.conf"_s));
    ASSERT_TRUE(store.save({output, microphone, disabled}).has_value());
    startDaemon();

    const std::optional<Graph::Node> sink = waitForNode(nodeNamed("reverblabr-virtual.0000000a"));
    ASSERT_TRUE(sink.has_value()) << "the nested daemon did not make the output";
    EXPECT_EQ(sink->prop("node.description"), "Everything \"else\" — ünï");
    EXPECT_EQ(sink->prop("media.class"), "Audio/Sink");
    EXPECT_EQ(sink->prop("reverblabr.virtual-device"), "user");
    EXPECT_EQ(sink->prop("node.virtual"), "true");

    const std::optional<Graph::Node> mic = waitForNode(nodeNamed("reverblabr-virtual.0000000b"));
    ASSERT_TRUE(mic.has_value()) << "the nested daemon did not make the microphone";
    EXPECT_EQ(mic->prop("node.description"), "Processed mic");
    EXPECT_EQ(mic->prop("media.class"), "Audio/Source/Virtual");

    // A daemon creates its configuration's objects before it takes a single
    // client, so the disabled one would already be here.
    const Graph graph = Graph::capture();
    EXPECT_EQ(graph.findNode(nodeNamed("reverblabr-virtual.0000000c")), nullptr);
}

// Both scopes' files are read together (spike §5), each device marked with
// the scope it came from.
TEST_F(VirtualDeviceFiles, TheUserAndTheSystemFileAreBothRead)
{
    const VirtualDeviceStore user(VirtualDeviceScope::User, dropIn(u"90-reverblabr-virtual-devices.conf"_s));
    const VirtualDeviceStore system(VirtualDeviceScope::System, dropIn(u"90-reverblabr-virtual-devices-system.conf"_s));
    ASSERT_TRUE(user.save({device(u"000000a1"_s, u"Mine"_s, VirtualDeviceScope::User)}).has_value());
    ASSERT_TRUE(system.save({device(u"000000b1"_s, u"Everybody's"_s, VirtualDeviceScope::System)}).has_value());
    startDaemon();

    const std::optional<Graph::Node> mine = waitForNode(nodeNamed("reverblabr-virtual.000000a1"));
    const std::optional<Graph::Node> theirs = waitForNode(nodeNamed("reverblabr-virtual.000000b1"));
    ASSERT_TRUE(mine.has_value());
    ASSERT_TRUE(theirs.has_value());
    EXPECT_EQ(mine->prop("reverblabr.virtual-device"), "user");
    EXPECT_EQ(theirs->prop("reverblabr.virtual-device"), "system");
}

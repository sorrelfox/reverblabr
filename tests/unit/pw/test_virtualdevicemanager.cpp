// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The virtual device manager's own bookkeeping: what it writes and
// says while there is no PipeWire to talk to. What it does to the graph is
// tests/pw/test_virtualdevicemanager.cpp.

#include "pw/connection.h"
#include "pw/registrymirror.h"
#include "pw/virtualdeviceconf.h"
#include "pw/virtualdevicemanager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <memory>

using namespace Qt::StringLiterals;
using namespace rb::pw;

namespace
{

int argc = 1;
char programName[] = "reverblabr-pw-test";
char *argv[] = {programName, nullptr};

[[nodiscard]] QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

} // namespace

/// A manager that was never connected: every change goes to the files, and
/// every device waits for PipeWire.
class VirtualDeviceManagerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        if (QCoreApplication::instance() == nullptr) {
            m_app = std::make_unique<QCoreApplication>(argc, argv);
        }
        ASSERT_TRUE(m_files.isValid());
        m_manager = std::make_unique<VirtualDeviceManager>(m_connection, m_mirror);
        m_manager->setStores(VirtualDeviceStore(VirtualDeviceScope::User, userFile()),
                             VirtualDeviceStore(VirtualDeviceScope::System, systemFile()));
        QObject::connect(m_manager.get(), &VirtualDeviceManager::devicesChanged, [this] { ++m_changes; });
    }

    void TearDown() override
    {
        m_manager.reset();
    }

    [[nodiscard]] QString userFile() const
    {
        return m_files.filePath(u"user/devices.conf"_s);
    }

    [[nodiscard]] QString systemFile() const
    {
        return m_files.filePath(u"system/devices.conf"_s);
    }

    QTemporaryDir m_files;
    Connection m_connection;
    RegistryMirror m_mirror{m_connection};
    std::unique_ptr<VirtualDeviceManager> m_manager;
    int m_changes = 0;

private:
    std::unique_ptr<QCoreApplication> m_app;
};

TEST_F(VirtualDeviceManagerTest, EveryChangeIsWrittenAndSaidWhilePipeWireIsAway)
{
    const auto made =
        m_manager->create(u" Podcast mix "_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Stereo);
    ASSERT_TRUE(made.has_value());
    EXPECT_EQ(made->name, u"Podcast mix"_s);
    EXPECT_TRUE(isValidVirtualDeviceId(made->id));
    EXPECT_EQ(m_changes, 1);
    ASSERT_EQ(m_manager->devices().size(), 1);
    EXPECT_EQ(m_manager->devices().first().status, VirtualDeviceStatus::WaitingForPipeWire);

    ASSERT_TRUE(m_manager->rename(made->id, u"Show mix"_s).has_value());
    ASSERT_TRUE(m_manager->setEnabled(made->id, false).has_value());
    EXPECT_EQ(m_manager->device(made->id)->status, VirtualDeviceStatus::Disabled);
    EXPECT_EQ(m_changes, 3);

    const auto onDisk = VirtualDeviceStore(VirtualDeviceScope::User, userFile()).load();
    ASSERT_TRUE(onDisk.has_value());
    ASSERT_EQ(onDisk->size(), 1);
    EXPECT_EQ(onDisk->first().name, u"Show mix"_s);
    EXPECT_FALSE(onDisk->first().enabled);

    // Saying the same thing again changes nothing, and says nothing.
    ASSERT_TRUE(m_manager->rename(made->id, u"Show mix"_s).has_value());
    ASSERT_TRUE(m_manager->setEnabled(made->id, false).has_value());
    EXPECT_EQ(m_changes, 3);

    ASSERT_TRUE(m_manager->remove(made->id).has_value());
    EXPECT_TRUE(m_manager->devices().isEmpty());
    EXPECT_FALSE(QFile::exists(userFile()));
}

TEST_F(VirtualDeviceManagerTest, ABadNameOrAnUnknownIdChangesNothing)
{
    const auto made = m_manager->create(u"Mic"_s, VirtualDeviceDirection::Input, VirtualDeviceChannels::Mono);
    ASSERT_TRUE(made.has_value());
    const QByteArray before = readAll(userFile());

    EXPECT_EQ(m_manager->create(u"   "_s, VirtualDeviceDirection::Input, VirtualDeviceChannels::Mono).error(),
              VirtualDeviceError::EmptyName);
    EXPECT_EQ(m_manager->rename(made->id, u"two\nlines"_s).error(), VirtualDeviceError::NameHasControlCharacter);
    EXPECT_EQ(m_manager->rename(u"00000000"_s, u"Nobody"_s).error(), VirtualDeviceError::NotFound);
    EXPECT_EQ(m_manager->setEnabled(u"00000000"_s, false).error(), VirtualDeviceError::NotFound);
    EXPECT_EQ(m_manager->remove(u"00000000"_s).error(), VirtualDeviceError::NotFound);

    EXPECT_EQ(readAll(userFile()), before);
    EXPECT_EQ(m_manager->device(made->id)->device.name, u"Mic"_s);
}

TEST_F(VirtualDeviceManagerTest, AllUsersDevicesAreListedAndLeftToTheHelper)
{
    VirtualDevice everyone;
    everyone.id = u"5ca1ab1e"_s;
    everyone.name = u"Shared output"_s;
    everyone.scope = VirtualDeviceScope::System;
    ASSERT_TRUE(VirtualDeviceStore(VirtualDeviceScope::System, systemFile()).save({everyone}));
    m_manager->reload();

    ASSERT_EQ(m_manager->devices().size(), 1);
    EXPECT_EQ(m_manager->devices().first().device, everyone);
    const QByteArray before = readAll(systemFile());
    EXPECT_EQ(m_manager->rename(everyone.id, u"Mine"_s).error(), VirtualDeviceError::ReadOnlyScope);
    EXPECT_EQ(m_manager->setEnabled(everyone.id, false).error(), VirtualDeviceError::ReadOnlyScope);
    EXPECT_EQ(m_manager->remove(everyone.id).error(), VirtualDeviceError::ReadOnlyScope);
    EXPECT_EQ(readAll(systemFile()), before);

    // A new device's id is unique across both files, since PipeWire loads both.
    const auto made = m_manager->create(u"Mine"_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Stereo);
    ASSERT_TRUE(made.has_value());
    EXPECT_NE(made->id, everyone.id);
    EXPECT_NE(m_manager->newId(), everyone.id);
}

TEST_F(VirtualDeviceManagerTest, AFileThatIsNotOursIsNeitherListedNorWrittenOver)
{
    ASSERT_TRUE(QDir().mkpath(QFileInfo(userFile()).path()));
    QFile foreign(userFile());
    ASSERT_TRUE(foreign.open(QIODevice::WriteOnly));
    foreign.write("context.objects = [ ]\n");
    foreign.close();
    m_manager->reload();

    EXPECT_EQ(m_manager->scopeError(VirtualDeviceScope::User), VirtualDeviceError::NotOurs);
    EXPECT_FALSE(m_manager->scopeError(VirtualDeviceScope::System).has_value());
    EXPECT_EQ(m_manager->create(u"Anything"_s, VirtualDeviceDirection::Output, VirtualDeviceChannels::Stereo).error(),
              VirtualDeviceError::NotOurs);
    EXPECT_EQ(readAll(userFile()), "context.objects = [ ]\n"_ba);
}

TEST_F(VirtualDeviceManagerTest, TheFacadeGetsEveryFieldByName)
{
    const auto made = m_manager->create(u"Voice"_s, VirtualDeviceDirection::Input, VirtualDeviceChannels::Mono);
    ASSERT_TRUE(made.has_value());
    const QJsonArray json = virtualDevicesToJson(m_manager->devices());
    ASSERT_EQ(json.size(), 1);
    const QJsonObject object = json.first().toObject();
    EXPECT_EQ(object.value(u"id"_s).toString(), made->id);
    EXPECT_EQ(object.value(u"name"_s).toString(), u"Voice"_s);
    EXPECT_EQ(object.value(u"nodeName"_s).toString(), u"reverblabr-virtual."_s + made->id);
    EXPECT_EQ(object.value(u"direction"_s).toString(), u"input"_s);
    EXPECT_EQ(object.value(u"channels"_s).toString(), u"mono"_s);
    EXPECT_TRUE(object.value(u"enabled"_s).toBool());
    EXPECT_EQ(object.value(u"scope"_s).toString(), u"user"_s);
    EXPECT_EQ(object.value(u"status"_s).toString(), u"waitingForPipeWire"_s);
}

TEST(VirtualDeviceNodeProperties, AreTheSameAsTheFilesEntry)
{
    // A node made now and a node PipeWire makes from the file at its next
    // start must be the same device, so every property the live node gets is
    // one the entry has, with the same value.
    VirtualDevice device;
    device.id = u"0123abcd"_s;
    device.name = u"Say \"hi\""_s;
    device.channels = VirtualDeviceChannels::Mono;
    const QList<VirtualDevice> read =
        virtualDevicesFromConf(virtualDevicesToConf({device}, VirtualDeviceScope::User), VirtualDeviceScope::User)
            .value();
    ASSERT_EQ(read.size(), 1);

    const QByteArray entry = virtualDevicesToConf({device}, VirtualDeviceScope::User);
    for (const auto &[key, value] : virtualDeviceNodeProperties(device, VirtualDeviceScope::User)) {
        EXPECT_TRUE(entry.contains(QByteArray(key + ' '))) << key.constData();
    }
    const auto properties = virtualDeviceNodeProperties(device, VirtualDeviceScope::System);
    const auto valueOf = [&](const QByteArray &key) {
        for (const auto &[name, value] : properties) {
            if (name == key) {
                return value;
            }
        }
        return QByteArray();
    };
    EXPECT_EQ(valueOf("node.name"_ba), "reverblabr-virtual.0123abcd"_ba);
    EXPECT_EQ(valueOf("node.description"_ba), "Say \"hi\""_ba);
    EXPECT_EQ(valueOf("media.class"_ba), "Audio/Sink"_ba);
    EXPECT_EQ(valueOf("audio.position"_ba), "MONO"_ba);
    EXPECT_EQ(valueOf("reverblabr.virtual-device"_ba), "system"_ba);
    EXPECT_EQ(valueOf("node.virtual"_ba), "true"_ba);
}

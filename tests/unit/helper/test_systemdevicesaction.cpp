// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the all-users helper does as root (ADR-0013), run against a
// directory of the test's own: the one file it writes, and everything it
// refuses. Whether the system bus, polkit and KAuth start it is checked by
// hand on a real desktop (docs/development.md).

#include "helper/systemdevicesaction.h"
#include "pw/virtualdeviceconf.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <ostream>

// A reason reads as words in a failure, not as UTF-16 code units.
QT_BEGIN_NAMESPACE
inline void PrintTo(const QString &text, std::ostream *out)
{
    *out << '"' << text.toStdString() << '"';
}
QT_END_NAMESPACE

using namespace Qt::StringLiterals;
using namespace rb::pw;
using rb::helper::writeSystemDevices;

namespace
{

[[nodiscard]] VirtualDevice device(const QString &id, const QString &name, bool enabled = true)
{
    VirtualDevice made;
    made.id = id;
    made.name = name;
    made.enabled = enabled;
    made.scope = VirtualDeviceScope::System;
    return made;
}

[[nodiscard]] QVariantMap request(const QList<VirtualDevice> &devices, const QList<VirtualDevice> &previous = {})
{
    return {{u"devices"_s, virtualDevicesToVariant(devices)}, {u"previous"_s, virtualDevicesToVariant(previous)}};
}

/// Every file under \a root, relative to it.
[[nodiscard]] QStringList filesUnder(const QString &root)
{
    QStringList files;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(QDir(root).relativeFilePath(it.next()));
    }
    files.sort();
    return files;
}

[[nodiscard]] QByteArray readAll(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void writeAll(const QString &path, const QByteArray &bytes)
{
    ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(bytes), bytes.size());
}

/// The reason, or "(written)" when the call succeeded, so that a test that
/// expected a refusal fails with something to read.
[[nodiscard]] QString outcome(const std::expected<void, QString> &result)
{
    return result ? u"(written)"_s : result.error();
}

class SystemDevicesAction : public ::testing::Test
{
protected:
    QTemporaryDir root;
    QString file;

    void SetUp() override
    {
        ASSERT_TRUE(root.isValid());
        file = VirtualDeviceStore::systemFile(root.path());
    }

    [[nodiscard]] QList<VirtualDevice> inTheFile() const
    {
        const auto loaded = VirtualDeviceStore(VirtualDeviceScope::System, file).load();
        return loaded ? *loaded : QList<VirtualDevice>{};
    }
};

} // namespace

TEST_F(SystemDevicesAction, WritesItsOneFileAndMakesTheDirectoriesForIt)
{
    // /etc/pipewire does not exist on a fresh Fedora 44.
    ASSERT_FALSE(QFileInfo::exists(root.filePath(u"etc"_s)));

    const QList<VirtualDevice> devices{device(u"0a1b2c3d"_s, u"Studio mix"_s)};
    EXPECT_EQ(outcome(writeSystemDevices(request(devices), root.path())), u"(written)"_s);

    EXPECT_EQ(filesUnder(root.path()),
              QStringList{u"etc/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices-system.conf"_s});
    EXPECT_EQ(inTheFile(), devices);
    // Every user's PipeWire reads it.
    EXPECT_TRUE(QFileInfo(file).permissions().testFlag(QFileDevice::ReadOther));
}

TEST_F(SystemDevicesAction, AChangeStartsFromTheListInTheFile)
{
    const QList<VirtualDevice> first{device(u"0a1b2c3d"_s, u"Studio mix"_s)};
    ASSERT_TRUE(writeSystemDevices(request(first), root.path()));

    QList<VirtualDevice> renamed = first;
    renamed[0].name = u"Broadcast mix"_s;
    renamed[0].enabled = false;
    EXPECT_EQ(outcome(writeSystemDevices(request(renamed, first), root.path())), u"(written)"_s);
    EXPECT_EQ(inTheFile(), renamed);
}

TEST_F(SystemDevicesAction, AListThatNoLongerMatchesTheFileIsRefused)
{
    // Another user added a device after this one's list was read: writing
    // this one's list would delete theirs without a word.
    const QList<VirtualDevice> theirs{device(u"0a1b2c3d"_s, u"Theirs"_s)};
    ASSERT_TRUE(writeSystemDevices(request(theirs), root.path()));
    const QByteArray before = readAll(file);

    const QList<VirtualDevice> mine{device(u"11111111"_s, u"Mine"_s)};
    EXPECT_EQ(outcome(writeSystemDevices(request(mine, {}), root.path())), QString(rb::helper::kChangedElsewhere));
    EXPECT_EQ(readAll(file), before);
}

TEST_F(SystemDevicesAction, AnEmptyListRemovesTheFile)
{
    const QList<VirtualDevice> devices{device(u"0a1b2c3d"_s, u"Studio mix"_s)};
    ASSERT_TRUE(writeSystemDevices(request(devices), root.path()));

    EXPECT_EQ(outcome(writeSystemDevices(request({}, devices), root.path())), u"(written)"_s);
    EXPECT_FALSE(QFileInfo::exists(file));
}

TEST_F(SystemDevicesAction, AFileItDidNotWriteIsLeftAlone)
{
    const QByteArray someoneElses = "context.objects = [ ]\n";
    writeAll(file, someoneElses);

    EXPECT_EQ(outcome(writeSystemDevices(request({device(u"0a1b2c3d"_s, u"Mix"_s)}), root.path())), u"NotOurs"_s);
    EXPECT_EQ(readAll(file), someoneElses);
}

TEST_F(SystemDevicesAction, BadDevicesAreRefusedAndNothingIsWritten)
{
    struct Case {
        const char *what;
        QList<VirtualDevice> devices;
        QString reason;
    };
    const QList<Case> cases{
        {"an empty name", {device(u"0a1b2c3d"_s, u"   "_s)}, u"EmptyName"_s},
        {"a line break", {device(u"0a1b2c3d"_s, u"Mix\nnode.name = x"_s)}, u"NameHasControlCharacter"_s},
        {"white space around", {device(u"0a1b2c3d"_s, u" Mix"_s)}, u"NameNotTrimmed"_s},
        {"65 characters", {device(u"0a1b2c3d"_s, QString(65, u'a'))}, u"NameTooLong"_s},
        {"a bad id", {device(u"../../x"_s, u"Mix"_s)}, u"InvalidId"_s},
        {"an upper-case id", {device(u"0A1B2C3D"_s, u"Mix"_s)}, u"InvalidId"_s},
        {"two with one id", {device(u"0a1b2c3d"_s, u"A"_s), device(u"0a1b2c3d"_s, u"B"_s)}, u"DuplicateId"_s},
    };
    for (const Case &c : cases) {
        EXPECT_EQ(outcome(writeSystemDevices(request(c.devices), root.path())), c.reason) << c.what;
    }

    QList<VirtualDevice> tooMany;
    for (int i = 0; i <= kMaxVirtualDevices; ++i) {
        tooMany.append(device(u"%1"_s.arg(i, 8, 16, u'0'), u"Device %1"_s.arg(i)));
    }
    EXPECT_EQ(outcome(writeSystemDevices(request(tooMany), root.path())), u"TooManyDevices"_s);

    EXPECT_EQ(filesUnder(root.path()), QStringList{});
}

TEST_F(SystemDevicesAction, ANameCannotChooseWhereTheFileGoes)
{
    // A name is only ever the device's description: the path is fixed.
    const QList<VirtualDevice> devices{device(u"0a1b2c3d"_s, u"../../../../tmp/owned"_s),
                                       device(u"0a1b2c3e"_s, u"/etc/shadow"_s)};
    EXPECT_EQ(outcome(writeSystemDevices(request(devices), root.path())), u"(written)"_s);
    EXPECT_EQ(filesUnder(root.path()),
              QStringList{u"etc/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices-system.conf"_s});
    EXPECT_EQ(inTheFile(), devices);
}

TEST_F(SystemDevicesAction, ArgumentsOfAnyOtherShapeAreRefused)
{
    const QVariantList good = virtualDevicesToVariant({device(u"0a1b2c3d"_s, u"Mix"_s)});
    QVariantMap entry = good.first().toMap();

    struct Case {
        const char *what;
        QVariantMap arguments;
        QString reason;
    };
    QVariantMap withPath = request({});
    withPath.insert(u"path"_s, u"/etc/passwd"_s);
    QVariantMap noPrevious{{u"devices"_s, good}};
    QVariantMap extraKey = entry;
    extraKey.insert(u"scope"_s, u"user"_s);
    QVariantMap missingKey = entry;
    missingKey.remove(u"enabled"_s);
    QVariantMap numberName = entry;
    numberName.insert(u"name"_s, 42);
    QVariantMap stringEnabled = entry;
    stringEnabled.insert(u"enabled"_s, u"true"_s);
    QVariantMap badDirection = entry;
    badDirection.insert(u"direction"_s, u"sideways"_s);
    QVariantMap badChannels = entry;
    badChannels.insert(u"channels"_s, u"surround"_s);
    QVariantMap hugeName = entry;
    hugeName.insert(u"name"_s, QString(1 << 20, u'a'));

    const QList<Case> cases{
        {"a path argument", withPath, u"BadRequest"_s},
        {"no previous list", noPrevious, u"BadRequest"_s},
        {"no arguments", {}, u"BadRequest"_s},
        {"devices not a list", {{u"devices"_s, u"x"_s}, {u"previous"_s, QVariantList{}}}, u"Malformed"_s},
        {"an entry not a map", {{u"devices"_s, QVariantList{u"x"_s}}, {u"previous"_s, QVariantList{}}}, u"Malformed"_s},
        {"an extra key", {{u"devices"_s, QVariantList{extraKey}}, {u"previous"_s, QVariantList{}}}, u"Malformed"_s},
        {"a missing key", {{u"devices"_s, QVariantList{missingKey}}, {u"previous"_s, QVariantList{}}}, u"Malformed"_s},
        {"a number for a name",
         {{u"devices"_s, QVariantList{numberName}}, {u"previous"_s, QVariantList{}}},
         u"Malformed"_s},
        {"a word for enabled",
         {{u"devices"_s, QVariantList{stringEnabled}}, {u"previous"_s, QVariantList{}}},
         u"Malformed"_s},
        {"an unknown direction",
         {{u"devices"_s, QVariantList{badDirection}}, {u"previous"_s, QVariantList{}}},
         u"Malformed"_s},
        {"unknown channels",
         {{u"devices"_s, QVariantList{badChannels}}, {u"previous"_s, QVariantList{}}},
         u"Malformed"_s},
        {"a megabyte of name",
         {{u"devices"_s, QVariantList{hugeName}}, {u"previous"_s, QVariantList{}}},
         u"NameTooLong"_s},
    };
    for (const Case &c : cases) {
        EXPECT_EQ(outcome(writeSystemDevices(c.arguments, root.path())), c.reason) << c.what;
    }
    EXPECT_EQ(filesUnder(root.path()), QStringList{});
}

TEST_F(SystemDevicesAction, AListLongerThanAnyFileMayHoldIsRefusedBeforeItIsRead)
{
    QVariantList entries;
    const QVariant one = virtualDevicesToVariant({device(u"0a1b2c3d"_s, u"Mix"_s)}).first();
    for (int i = 0; i < 100000; ++i) {
        entries.append(one);
    }
    EXPECT_EQ(outcome(writeSystemDevices({{u"devices"_s, entries}, {u"previous"_s, QVariantList{}}}, root.path())),
              u"TooManyDevices"_s);
    EXPECT_EQ(filesUnder(root.path()), QStringList{});
}

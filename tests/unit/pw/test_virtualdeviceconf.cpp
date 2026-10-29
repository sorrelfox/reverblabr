// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The virtual devices' drop-in (ADR-0013): what it says, how it is read
// back, and when it is left alone. Whether PipeWire makes the devices it
// describes is tests/pw/test_virtualdevices.cpp.

#include "pw/virtualdeviceconf.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <unistd.h>

using namespace Qt::StringLiterals;
using namespace rb::pw;

namespace
{

[[nodiscard]] VirtualDevice
device(const QString &id, const QString &name, VirtualDeviceScope scope = VirtualDeviceScope::User)
{
    VirtualDevice made;
    made.id = id;
    made.name = name;
    made.scope = scope;
    return made;
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

[[nodiscard]] std::expected<QList<VirtualDevice>, VirtualDeviceError> roundTrip(const QList<VirtualDevice> &devices,
                                                                                VirtualDeviceScope scope)
{
    return virtualDevicesFromConf(virtualDevicesToConf(devices, scope), scope);
}

/// The error, or nothing when there was a value: a test that expected an
/// error and got a value fails instead of stopping the whole run.
template <typename T>
[[nodiscard]] std::optional<VirtualDeviceError> errorOf(const std::expected<T, VirtualDeviceError> &result)
{
    return result ? std::nullopt : std::optional(result.error());
}

} // namespace

// ---- The text --------------------------------------------------------------

// The file exactly as ADR-0013 describes it, so that a change to the format is
// a change somebody has to mean.
TEST(VirtualDeviceConf, WritesTheEntryTheDecisionRecordDescribes)
{
    VirtualDevice output = device(u"0a1b2c3d"_s, u"Podcast \"mix\""_s);
    VirtualDevice microphone = device(u"deadbeef"_s, u"Call mic"_s);
    microphone.direction = VirtualDeviceDirection::Input;
    microphone.channels = VirtualDeviceChannels::Mono;
    microphone.enabled = false;

    const QByteArray expected = R"(# Managed by Reverblabr: this user's virtual audio devices.
# Reverblabr writes this file whole, so changes made here are lost.
# Remove a device in Reverblabr, or delete this file to remove them all.

context.objects = [
    { factory = adapter
      args = {
        factory.name              = support.null-audio-sink
        node.name                 = "reverblabr-virtual.0a1b2c3d"
        node.description          = "Podcast \"mix\""
        media.class               = Audio/Sink
        audio.position            = [ FL FR ]
        node.virtual              = true
        monitor.channel-volumes   = true
        reverblabr.virtual-device = "user"
      }
      flags = [ nofail ]
    }
    { factory = adapter
      args = {
        factory.name              = support.null-audio-sink
        node.name                 = "reverblabr-virtual.deadbeef"
        node.description          = "Call mic"
        media.class               = Audio/Source/Virtual
        audio.position            = [ MONO ]
        node.virtual              = true
        monitor.channel-volumes   = true
        reverblabr.virtual-device = "user"
      }
      flags = [ nofail ]
      condition = [ { reverblabr.show-disabled-virtual-devices = true } ]
    }
]
)"_ba;
    EXPECT_EQ(virtualDevicesToConf({output, microphone}, VirtualDeviceScope::User), expected);
}

TEST(VirtualDeviceConf, TheSystemFileSaysWhoseItIsAndMarksItsDevicesSo)
{
    const QByteArray text = virtualDevicesToConf({device(u"12345678"_s, u"Shared"_s, VirtualDeviceScope::System)},
                                                 VirtualDeviceScope::System);
    EXPECT_TRUE(text.startsWith("# Managed by Reverblabr: virtual audio devices for every user"));
    EXPECT_TRUE(text.contains(R"(reverblabr.virtual-device = "system")"));
}

TEST(VirtualDeviceConf, EveryFieldCombinationSurvivesTheRoundTrip)
{
    for (const VirtualDeviceScope scope : {VirtualDeviceScope::User, VirtualDeviceScope::System}) {
        QList<VirtualDevice> devices;
        int n = 0;
        for (const VirtualDeviceDirection direction : {VirtualDeviceDirection::Output, VirtualDeviceDirection::Input}) {
            for (const VirtualDeviceChannels channels : {VirtualDeviceChannels::Mono, VirtualDeviceChannels::Stereo}) {
                for (const bool enabled : {true, false}) {
                    VirtualDevice made = device(u"0000000%1"_s.arg(n), u"Device %1"_s.arg(n), scope);
                    made.direction = direction;
                    made.channels = channels;
                    made.enabled = enabled;
                    devices += made;
                    ++n;
                }
            }
        }
        const auto back = roundTrip(devices, scope);
        ASSERT_TRUE(back.has_value()) << virtualDeviceErrorName(back.error());
        EXPECT_EQ(*back, devices);
    }
}

TEST(VirtualDeviceConf, NoDevicesIsAFileWithAnEmptyList)
{
    const auto back = roundTrip({}, VirtualDeviceScope::User);
    ASSERT_TRUE(back.has_value());
    EXPECT_TRUE(back->isEmpty());
}

// Names somebody could type that mean something to SPA-JSON or to a shell:
// each comes back exactly as it went in.
TEST(VirtualDeviceConf, NamesThatLookLikeSyntaxComeBackUnchanged)
{
    const QStringList names = {
        u"Quote \" inside"_s,
        u"Back\\slash"_s,
        u"Trailing backslash \\"_s,
        u"\\\" escaped quote"_s,
        u"Braces } ] { ["_s,
        u"key = value, # not a comment"_s,
        u"'single' and `back`"_s,
        u"\\u0041 is not A"_s,
        u"Кириллица"_s,
        u"العربية من اليمين"_s,
        u"日本語のマイク"_s,
        u"Emoji 🎙️🎧"_s,
        u"x"_s,
        QString(kMaxVirtualDeviceNameLength, u'n'),
    };
    QList<VirtualDevice> devices;
    for (qsizetype i = 0; i < names.size(); ++i) {
        ASSERT_TRUE(validVirtualDeviceName(names[i]).has_value()) << names[i].toStdString();
        devices += device(u"%1"_s.arg(i, 8, 16, u'0'), names[i]);
    }
    const auto back = roundTrip(devices, VirtualDeviceScope::User);
    ASSERT_TRUE(back.has_value()) << virtualDeviceErrorName(back.error());
    ASSERT_EQ(back->size(), devices.size());
    for (qsizetype i = 0; i < devices.size(); ++i) {
        EXPECT_EQ(back->at(i).name, names[i]) << names[i].toStdString();
    }
}

// The writer escapes control characters although validation refuses them,
// so a list nobody validated still cannot end its string and add a property.
TEST(VirtualDeviceConf, AControlCharacterCannotBreakOutOfItsString)
{
    const QByteArray text =
        virtualDevicesToConf({device(u"0000000a"_s, u"a\n\" }\r\t\u0001\u007f"_s)}, VirtualDeviceScope::User);
    EXPECT_TRUE(text.contains(R"(node.description          = "a\n\" }\r\t\u0001\u007f")")) << text.toStdString();
    EXPECT_EQ(text.count('\n'),
              virtualDevicesToConf({device(u"0000000a"_s, u"a"_s)}, VirtualDeviceScope::User).count('\n'));
}

// ---- Names -----------------------------------------------------------------

TEST(VirtualDeviceConf, ANameIsTrimmedAndMustNotBeEmpty)
{
    EXPECT_EQ(validVirtualDeviceName(u"  Podcast mix  "_s).value_or(QString()), u"Podcast mix"_s);
    EXPECT_EQ(errorOf(validVirtualDeviceName(QString())), VirtualDeviceError::EmptyName);
    EXPECT_EQ(errorOf(validVirtualDeviceName(u"   　 "_s)), VirtualDeviceError::EmptyName);
}

// The limit counts characters, not the UTF-16 units a string stores: sixty-four
// emoji are sixty-four characters.
TEST(VirtualDeviceConf, ANameHasAtMostSixtyFourCharacters)
{
    QString emoji;
    for (int i = 0; i < kMaxVirtualDeviceNameLength; ++i) {
        emoji += u"🎧"_s;
    }
    EXPECT_TRUE(validVirtualDeviceName(emoji).has_value());
    EXPECT_EQ(errorOf(validVirtualDeviceName(emoji + u"🎧"_s)), VirtualDeviceError::NameTooLong);
    EXPECT_EQ(errorOf(validVirtualDeviceName(QString(kMaxVirtualDeviceNameLength + 1, u'n'))),
              VirtualDeviceError::NameTooLong);
}

TEST(VirtualDeviceConf, ANameWithALineBreakOrAControlCharacterIsRefused)
{
    const QStringList names = {
        u"two\nlines"_s,
        u"carriage\rreturn"_s,
        u"tab\there"_s,
        u"nul"_s + QChar(0) + u"inside"_s,
        u"delete\u007f here"_s,
        u"next\u0085line"_s,
        u"line separator"_s,
        u"paragraph separator"_s,
        u"lone "_s + QChar(0xd83c) + u" half"_s,
        u"reversed "_s + QChar(0xdfa7) + QChar(0xd83c),
    };
    for (const QString &name : names) {
        EXPECT_EQ(errorOf(validVirtualDeviceName(name)), VirtualDeviceError::NameHasControlCharacter)
            << name.toStdString();
    }
}

// ---- Lists and ids -----------------------------------------------------------

TEST(VirtualDeviceConf, AListIsCheckedWhole)
{
    const auto check = [](const QList<VirtualDevice> &devices) {
        const auto valid = validateVirtualDevices(devices, VirtualDeviceScope::User);
        return valid ? std::optional<VirtualDeviceError>() : std::optional(valid.error());
    };
    EXPECT_EQ(check({device(u"0000000a"_s, u"A"_s)}), std::nullopt);
    EXPECT_EQ(check({device(u"0000000A"_s, u"A"_s)}), VirtualDeviceError::InvalidId);
    EXPECT_EQ(check({device(u"0000000"_s, u"A"_s)}), VirtualDeviceError::InvalidId);
    EXPECT_EQ(check({device(u"0000000g"_s, u"A"_s)}), VirtualDeviceError::InvalidId);
    EXPECT_EQ(check({device(u"0000000a"_s, u"A"_s), device(u"0000000a"_s, u"B"_s)}), VirtualDeviceError::DuplicateId);
    EXPECT_EQ(check({device(u"0000000a"_s, u"A"_s, VirtualDeviceScope::System)}), VirtualDeviceError::WrongScope);
    EXPECT_EQ(check({device(u"0000000a"_s, u" A"_s)}), VirtualDeviceError::NameNotTrimmed);
    EXPECT_EQ(check({device(u"0000000a"_s, u"a\nb"_s)}), VirtualDeviceError::NameHasControlCharacter);

    QList<VirtualDevice> many;
    for (int i = 0; i <= kMaxVirtualDevices; ++i) {
        many += device(u"%1"_s.arg(i, 8, 16, u'0'), u"D"_s);
    }
    EXPECT_EQ(check(many), VirtualDeviceError::TooManyDevices);
    many.removeLast();
    EXPECT_EQ(check(many), std::nullopt);
}

TEST(VirtualDeviceConf, NewIdsAreEightHexDigitsAndNeverOneAlreadyTaken)
{
    QList<VirtualDevice> taken;
    QSet<QString> seen;
    for (int i = 0; i < 2000; ++i) {
        const QString id = newVirtualDeviceId(taken);
        ASSERT_TRUE(isValidVirtualDeviceId(id)) << id.toStdString();
        ASSERT_FALSE(seen.contains(id));
        seen.insert(id);
        taken += device(id, u"D"_s);
    }
    EXPECT_EQ(virtualDeviceNodeName(u"0a1b2c3d"_s), u"reverblabr-virtual.0a1b2c3d"_s);
}

// ---- Reading what somebody else wrote --------------------------------------

TEST(VirtualDeviceConf, TextWithoutOurHeaderIsNotOurs)
{
    const QByteArray theirs = "context.objects = [ ]\n";
    EXPECT_EQ(errorOf(virtualDevicesFromConf(theirs, VirtualDeviceScope::User)), VirtualDeviceError::NotOurs);
    EXPECT_EQ(errorOf(virtualDevicesFromConf({}, VirtualDeviceScope::User)), VirtualDeviceError::NotOurs);
    // The header has to come first; further down it is only a comment.
    EXPECT_EQ(
        errorOf(virtualDevicesFromConf("\n# Managed by Reverblabr\ncontext.objects = [ ]\n", VirtualDeviceScope::User)),
        VirtualDeviceError::NotOurs);
}

TEST(VirtualDeviceConf, OurHeaderOverTextWeDidNotWriteIsMalformed)
{
    const QByteArray good = virtualDevicesToConf({device(u"0000000a"_s, u"A"_s)}, VirtualDeviceScope::User);
    const auto malformed = [](const QByteArray &text) {
        const auto read = virtualDevicesFromConf(text, VirtualDeviceScope::User);
        return !read && read.error() == VirtualDeviceError::Malformed;
    };
    // Cut off anywhere inside the list.
    for (const qsizetype cut : {good.indexOf("context") + 3,
                                good.indexOf("args") + 3,
                                good.indexOf("Audio/") + 3,
                                good.indexOf("nofail") + 3,
                                good.lastIndexOf('}'),
                                good.lastIndexOf(']')}) {
        EXPECT_TRUE(malformed(good.left(cut))) << good.left(cut).toStdString();
    }
    QByteArray changed = good;
    EXPECT_TRUE(malformed(changed.replace("support.null-audio-sink", "api.alsa.pcm.sink")));
    changed = good;
    EXPECT_TRUE(malformed(changed.replace("reverblabr-virtual.", "somebody.")));
    changed = good;
    EXPECT_TRUE(malformed(changed.replace("Audio/Sink", "Video/Source")));
    changed = good;
    EXPECT_TRUE(malformed(changed.replace("[ FL FR ]", "[ FL FR RL RR ]")));
    changed = good;
    EXPECT_TRUE(malformed(changed.replace("flags = [ nofail ]", "flags = [ nofail ]\n      extra = 1")));
    changed = good;
    EXPECT_TRUE(malformed(changed.replace("flags = [ nofail ]", "condition = [ { core.daemon = true } ]")));
    changed = good;
    EXPECT_TRUE(malformed(changed.append("context.modules = [ ]\n")));
    // A device of the other scope, which the other file holds.
    changed = good;
    EXPECT_TRUE(malformed(changed.replace(R"("user")", R"("system")")));
    // The same id twice.
    EXPECT_TRUE(malformed(virtualDevicesToConf({device(u"0000000a"_s, u"A"_s), device(u"0000000a"_s, u"B"_s)},
                                               VirtualDeviceScope::User)));
}

// ---- The file on the disk --------------------------------------------------

TEST(VirtualDeviceStore, TheFilesLiveWherePipeWireLooksForThem)
{
    // rb_add_gtest points XDG_CONFIG_HOME into the build tree, so this is not
    // the real ~/.config either; engine.TestHome checks that it arrives.
    const QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
    ASSERT_FALSE(config.isEmpty());

    EXPECT_EQ(VirtualDeviceStore::userFile(),
              config + u"/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices.conf"_s);
    EXPECT_EQ(VirtualDeviceStore::systemFile(),
              u"/etc/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices-system.conf"_s);
    EXPECT_EQ(VirtualDeviceStore::systemFile(u"/tmp/root/"_s),
              u"/tmp/root/etc/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices-system.conf"_s);
    EXPECT_EQ(VirtualDeviceStore(VirtualDeviceScope::User).path(), VirtualDeviceStore::userFile());
    EXPECT_EQ(VirtualDeviceStore(VirtualDeviceScope::System).path(), VirtualDeviceStore::systemFile());
}

TEST(VirtualDeviceStore, NoFileIsNoDevices)
{
    QTemporaryDir root;
    const VirtualDeviceStore store(VirtualDeviceScope::User, root.filePath(u"missing.conf"_s));
    const auto devices = store.load();
    ASSERT_TRUE(devices.has_value());
    EXPECT_TRUE(devices->isEmpty());
}

// Neither directory exists on a fresh Fedora 44 (spike §5).
TEST(VirtualDeviceStore, SavingMakesTheDirectoriesAndReadsBack)
{
    QTemporaryDir root;
    const VirtualDeviceStore store(VirtualDeviceScope::System, VirtualDeviceStore::systemFile(root.path()));
    VirtualDevice made = device(u"0a1b2c3d"_s, u"Shared mic"_s, VirtualDeviceScope::System);
    made.direction = VirtualDeviceDirection::Input;

    ASSERT_TRUE(store.save({made}).has_value());
    const auto back = store.load();
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, QList<VirtualDevice>{made});

    // Every user's PipeWire reads it; nobody else may write it.
    const QFileInfo info(store.path());
    EXPECT_EQ(info.permissions()
                  & (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::WriteGroup
                     | QFileDevice::ReadOther | QFileDevice::WriteOther),
              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
    // Nothing is left beside it.
    EXPECT_EQ(QDir(info.absolutePath()).entryList(QDir::Files), QStringList{info.fileName()});
}

TEST(VirtualDeviceStore, SavingReplacesTheOldList)
{
    QTemporaryDir root;
    const VirtualDeviceStore store(VirtualDeviceScope::User, root.filePath(u"d/devices.conf"_s));
    ASSERT_TRUE(store.save({device(u"0000000a"_s, u"A"_s), device(u"0000000b"_s, u"B"_s)}).has_value());
    VirtualDevice renamed = device(u"0000000b"_s, u"B renamed"_s);
    renamed.enabled = false;
    ASSERT_TRUE(store.save({renamed}).has_value());
    EXPECT_EQ(store.load().value_or(QList<VirtualDevice>{}), QList<VirtualDevice>{renamed});
}

TEST(VirtualDeviceStore, AnEmptyListRemovesTheFile)
{
    QTemporaryDir root;
    const VirtualDeviceStore store(VirtualDeviceScope::User, root.filePath(u"devices.conf"_s));
    ASSERT_TRUE(store.save({device(u"0000000a"_s, u"A"_s)}).has_value());
    ASSERT_TRUE(QFile::exists(store.path()));
    ASSERT_TRUE(store.save({}).has_value());
    EXPECT_FALSE(QFile::exists(store.path()));
    // And removing what is not there is not a failure.
    EXPECT_TRUE(store.save({}).has_value());
}

// Somebody's own drop-in, or one of ours that no longer reads: either may
// hold what the user wants, so it stays exactly as it is.
TEST(VirtualDeviceStore, AFileItCannotCallItsOwnIsLeftByteForByte)
{
    QTemporaryDir root;
    const QString path = root.filePath(u"devices.conf"_s);
    const VirtualDeviceStore store(VirtualDeviceScope::User, path);
    const QByteArray theirs = "# my own sinks\ncontext.objects = [ ]\n";
    const QByteArray broken = virtualDevicesToConf({device(u"0000000a"_s, u"A"_s)}, VirtualDeviceScope::User).left(200);

    for (const auto &[bytes, error] :
         {std::pair{theirs, VirtualDeviceError::NotOurs}, std::pair{broken, VirtualDeviceError::Malformed}}) {
        writeAll(path, bytes);
        EXPECT_EQ(errorOf(store.load()), error);
        EXPECT_EQ(errorOf(store.save({device(u"0000000b"_s, u"B"_s)})), error);
        EXPECT_EQ(errorOf(store.save({})), error);
        EXPECT_EQ(readAll(path), bytes);
    }
}

TEST(VirtualDeviceStore, AnInvalidListChangesNothing)
{
    QTemporaryDir root;
    const VirtualDeviceStore store(VirtualDeviceScope::User, root.filePath(u"devices.conf"_s));
    ASSERT_TRUE(store.save({device(u"0000000a"_s, u"A"_s)}).has_value());
    const QByteArray before = readAll(store.path());
    EXPECT_EQ(errorOf(store.save({device(u"0000000a"_s, u"A\nB"_s)})), VirtualDeviceError::NameHasControlCharacter);
    EXPECT_EQ(errorOf(store.save({device(u"0000000a"_s, u"A"_s, VirtualDeviceScope::System)})),
              VirtualDeviceError::WrongScope);
    EXPECT_EQ(readAll(store.path()), before);
}

TEST(VirtualDeviceStore, AFileThatCannotBeReadIsSaidSo)
{
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root reads every file";
    }
    QTemporaryDir root;
    const QString path = root.filePath(u"devices.conf"_s);
    writeAll(path, virtualDevicesToConf({device(u"0000000a"_s, u"A"_s)}, VirtualDeviceScope::User));
    ASSERT_TRUE(QFile::setPermissions(path, QFileDevice::WriteOwner));
    const VirtualDeviceStore store(VirtualDeviceScope::User, path);
    EXPECT_EQ(errorOf(store.load()), VirtualDeviceError::FileNotReadable);
    EXPECT_EQ(errorOf(store.save({})), VirtualDeviceError::FileNotReadable);
    EXPECT_TRUE(QFile::exists(path));
}

TEST(VirtualDeviceStore, ADirectoryThatCannotBeWrittenIsSaidSo)
{
    if (::geteuid() == 0) {
        GTEST_SKIP() << "root writes every directory";
    }
    QTemporaryDir root;
    ASSERT_TRUE(QDir(root.path()).mkdir(u"locked"_s));
    ASSERT_TRUE(QFile::setPermissions(root.filePath(u"locked"_s), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    const VirtualDeviceStore store(VirtualDeviceScope::User, root.filePath(u"locked/sub/devices.conf"_s));
    EXPECT_EQ(errorOf(store.save({device(u"0000000a"_s, u"A"_s)})), VirtualDeviceError::FileNotWritable);
    QFile::setPermissions(root.filePath(u"locked"_s),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

TEST(VirtualDeviceConf, AListForTheHelperComesBackAsItWent)
{
    VirtualDevice microphone = device(u"0000000b"_s, u"Call mic \"clean\""_s, VirtualDeviceScope::System);
    microphone.direction = VirtualDeviceDirection::Input;
    microphone.channels = VirtualDeviceChannels::Mono;
    microphone.enabled = false;
    const QList<VirtualDevice> devices{device(u"0000000a"_s, u"Mix"_s, VirtualDeviceScope::System), microphone};

    const auto back = virtualDevicesFromVariant(virtualDevicesToVariant(devices), VirtualDeviceScope::System);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, devices);
    // The scope is the reader's to say: the list carries none.
    EXPECT_EQ(errorOf(validateVirtualDevices(
                  *virtualDevicesFromVariant(virtualDevicesToVariant(devices), VirtualDeviceScope::User),
                  VirtualDeviceScope::System)),
              VirtualDeviceError::WrongScope);
}

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The list the all-users helper is asked to write, worked out from
// what the engine last listed.

#include "core/virtualdevices.h"
#include "helper/systemdevicesaction.h"
#include "pw/virtualdevicemanager.h"

#include <QSet>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] pw::VirtualDeviceState state(const QString &id, const QString &name, pw::VirtualDeviceScope scope)
{
    pw::VirtualDeviceState made;
    made.device.id = id;
    made.device.name = name;
    made.device.scope = scope;
    made.status = pw::VirtualDeviceStatus::Present;
    return made;
}

/// A VirtualDevicesChanged message listing one device of the user's own and
/// two for all users.
[[nodiscard]] QString listed(std::optional<pw::VirtualDeviceError> systemFile = std::nullopt)
{
    return core::virtualDevicesMessage({state(u"aaaaaaaa"_s, u"Mine"_s, pw::VirtualDeviceScope::User),
                                        state(u"bbbbbbbb"_s, u"Mix"_s, pw::VirtualDeviceScope::System),
                                        state(u"cccccccc"_s, u"Call"_s, pw::VirtualDeviceScope::System)},
                                       std::nullopt,
                                       systemFile);
}

[[nodiscard]] core::SystemDeviceChange
change(core::SystemDeviceChange::Kind kind, const QString &id = {}, const QString &name = {})
{
    core::SystemDeviceChange made;
    made.kind = kind;
    made.deviceId = id;
    made.name = name;
    return made;
}

[[nodiscard]] QStringList ids(const QList<pw::VirtualDevice> &devices)
{
    QStringList out;
    for (const pw::VirtualDevice &device : devices) {
        out.append(device.id);
    }
    return out;
}

} // namespace

TEST(SystemDeviceChange, TheHelperAndTheInterfaceUseTheSameWords)
{
    EXPECT_EQ(QString(core::kVirtualDeviceChangedElsewhere), QString(helper::kChangedElsewhere));
    EXPECT_EQ(QString(core::kVirtualDeviceBadRequest), QString(helper::kBadRequest));
}

TEST(SystemDeviceChange, AListingSeparatesTheScopes)
{
    const core::VirtualDeviceListing listing = core::virtualDeviceListingFromMessage(listed());
    EXPECT_EQ(ids(listing.user), QStringList{u"aaaaaaaa"_s});
    EXPECT_EQ(ids(listing.system), (QStringList{u"bbbbbbbb"_s, u"cccccccc"_s}));
    EXPECT_TRUE(listing.unreadable.isEmpty());
}

TEST(SystemDeviceChange, OnlyTheOneDeviceChanges)
{
    const auto renamed = core::planSystemDeviceChange(
        listed(), change(core::SystemDeviceChange::Kind::Rename, u"cccccccc"_s, u" Calls "_s));
    ASSERT_TRUE(renamed.has_value()) << renamed.error().toStdString();
    EXPECT_EQ(ids(renamed->previous), (QStringList{u"bbbbbbbb"_s, u"cccccccc"_s}));
    EXPECT_EQ(renamed->devices.at(0), renamed->previous.at(0));
    EXPECT_EQ(renamed->devices.at(1).name, u"Calls"_s);
    EXPECT_EQ(renamed->deviceName, u"Calls"_s);

    const auto removed =
        core::planSystemDeviceChange(listed(), change(core::SystemDeviceChange::Kind::Remove, u"bbbbbbbb"_s));
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(ids(removed->devices), QStringList{u"cccccccc"_s});
    EXPECT_EQ(removed->deviceName, u"Mix"_s);

    core::SystemDeviceChange off = change(core::SystemDeviceChange::Kind::SetEnabled, u"bbbbbbbb"_s);
    off.enabled = false;
    const auto disabled = core::planSystemDeviceChange(listed(), off);
    ASSERT_TRUE(disabled.has_value());
    EXPECT_FALSE(disabled->devices.at(0).enabled);
    EXPECT_TRUE(disabled->devices.at(1).enabled);
}

TEST(SystemDeviceChange, ANewIdIsFreeInBothFiles)
{
    for (int i = 0; i < 200; ++i) {
        const auto created =
            core::planSystemDeviceChange(listed(), change(core::SystemDeviceChange::Kind::Create, {}, u"Studio"_s));
        ASSERT_TRUE(created.has_value());
        EXPECT_FALSE((QSet<QString>{u"aaaaaaaa"_s, u"bbbbbbbb"_s, u"cccccccc"_s}).contains(created->deviceId));
        EXPECT_EQ(created->devices.back().id, created->deviceId);
        EXPECT_EQ(created->devices.back().scope, pw::VirtualDeviceScope::System);
    }
}

TEST(SystemDeviceChange, WhatCannotBeWrittenIsRefused)
{
    // The user's own device is not the helper's to change.
    EXPECT_EQ(
        core::planSystemDeviceChange(listed(), change(core::SystemDeviceChange::Kind::Remove, u"aaaaaaaa"_s)).error(),
        u"NotFound"_s);
    EXPECT_EQ(core::planSystemDeviceChange(listed(), change(core::SystemDeviceChange::Kind::Create, {}, u""_s)).error(),
              u"EmptyName"_s);
    // An unreadable system file: its devices are not known.
    EXPECT_EQ(core::planSystemDeviceChange(listed(pw::VirtualDeviceError::Malformed),
                                           change(core::SystemDeviceChange::Kind::Create, {}, u"Studio"_s))
                  .error(),
              u"Malformed"_s);

    QList<pw::VirtualDeviceState> full;
    for (int i = 0; i < pw::kMaxVirtualDevices; ++i) {
        full.append(state(u"%1"_s.arg(i, 8, 16, u'0'), u"Device %1"_s.arg(i), pw::VirtualDeviceScope::System));
    }
    EXPECT_EQ(core::planSystemDeviceChange(core::virtualDevicesMessage(full, std::nullopt, std::nullopt),
                                           change(core::SystemDeviceChange::Kind::Create, {}, u"One more"_s))
                  .error(),
              u"TooManyDevices"_s);
}

TEST(SystemDeviceChange, TheHelpersArgumentsAreTheTwoLists)
{
    const auto created =
        core::planSystemDeviceChange(listed(), change(core::SystemDeviceChange::Kind::Create, {}, u"Studio"_s));
    ASSERT_TRUE(created.has_value());
    const QVariantMap arguments = created->helperArguments();
    EXPECT_EQ(arguments.keys(), (QStringList{u"devices"_s, u"previous"_s}));
    EXPECT_EQ(pw::virtualDevicesFromVariant(arguments.value(u"devices"_s), pw::VirtualDeviceScope::System).value(),
              created->devices);
    EXPECT_EQ(pw::virtualDevicesFromVariant(arguments.value(u"previous"_s), pw::VirtualDeviceScope::System).value(),
              created->previous);
}

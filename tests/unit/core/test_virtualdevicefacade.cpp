// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Virtual devices through the facade: what a create request may say,
// what the list looks like, and the mock the interface is built against —
// which has to keep the real manager's rules, or a window written against it
// would be surprised by the real thing.

#include "core/errorcodes.h"
#include "core/mockengine.h"
#include "core/presetstore.h"
#include "core/virtualdevices.h"
#include "pw/virtualdevicemanager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] QJsonObject parse(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

[[nodiscard]] QString request(const QString &name, const QString &direction, const QString &channels = {})
{
    QJsonObject object{{"name"_L1, name}, {"direction"_L1, direction}};
    if (!channels.isEmpty()) {
        object.insert("channels"_L1, channels);
    }
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/// The mock, with a preset directory of the test's own, and ears on what it
/// says about virtual devices.
class MockVirtualDevices : public ::testing::Test
{
protected:
    QTemporaryDir presets;
    MockEngine engine{PresetStore{presets.path()}};
    QSignalSpy lists{&engine, &EngineFacade::virtualDevicesChanged};
    QSignalSpy created{&engine, &EngineFacade::virtualDeviceCreated};
    QSignalSpy errors{&engine, &EngineFacade::error};
    QSignalSpy inventory{&engine, &EngineFacade::inventoryChanged};

    [[nodiscard]] QJsonObject lastList() const
    {
        return lists.isEmpty() ? QJsonObject{} : parse(lists.back().at(0).toString());
    }

    [[nodiscard]] QJsonArray devices() const
    {
        return lastList().value("devices"_L1).toArray();
    }

    [[nodiscard]] QJsonObject device(const QString &id) const
    {
        for (const QJsonValue &listed : devices()) {
            if (listed.toObject().value("id"_L1).toString() == id) {
                return listed.toObject();
            }
        }
        return {};
    }

    /// The error last reported, as {code, reason}.
    [[nodiscard]] std::pair<QString, QString> lastError() const
    {
        if (errors.isEmpty()) {
            return {};
        }
        return {errors.back().at(0).toString(), parse(errors.back().at(1).toString()).value("reason"_L1).toString()};
    }

    [[nodiscard]] QString make(const QString &name, const QString &direction = u"output"_s)
    {
        engine.createVirtualDevice(request(name, direction));
        return created.isEmpty() ? QString() : created.back().at(0).toString();
    }

    [[nodiscard]] bool inInventory(const QString &nodeName, const char *list) const
    {
        const QJsonObject last = inventory.isEmpty() ? QJsonObject{} : parse(inventory.back().at(0).toString());
        for (const QJsonValue &entry : last.value(QLatin1StringView(list)).toArray()) {
            if (entry.toObject().value("node"_L1).toString() == nodeName) {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST(VirtualDeviceFacade, AReadableRequestSaysWhatKindOfDevice)
{
    const auto output = virtualDeviceRequestFromJson(request(u"Every app"_s, u"output"_s, u"mono"_s));
    ASSERT_TRUE(output.has_value());
    EXPECT_EQ(output->name, u"Every app"_s);
    EXPECT_EQ(output->direction, pw::VirtualDeviceDirection::Output);
    EXPECT_EQ(output->channels, pw::VirtualDeviceChannels::Mono);

    // Stereo when the channels are left out: what nearly everybody wants.
    const auto input = virtualDeviceRequestFromJson(request(u"Mic"_s, u"input"_s));
    ASSERT_TRUE(input.has_value());
    EXPECT_EQ(input->direction, pw::VirtualDeviceDirection::Input);
    EXPECT_EQ(input->channels, pw::VirtualDeviceChannels::Stereo);
}

TEST(VirtualDeviceFacade, ARequestThatMakesNoSenseIsABadRequest)
{
    for (const QString &json : {u"not json"_s,
                                u"[1, 2]"_s,
                                request(u"No direction"_s, QString()),
                                request(u"Sideways"_s, u"sideways"_s),
                                request(u"Quad"_s, u"output"_s, u"quad"_s)}) {
        SCOPED_TRACE(json.toStdString());
        const auto read = virtualDeviceRequestFromJson(json);
        ASSERT_FALSE(read.has_value());
        EXPECT_EQ(read.error(), QString(kVirtualDeviceBadRequest));
    }
    // The name is carried along all the same, so a refusal can say whose.
    EXPECT_EQ(virtualDeviceRequestName(request(u"Sideways"_s, u"sideways"_s)), u"Sideways"_s);
}

TEST(VirtualDeviceFacade, TheListNamesTheScopesThatCouldNotBeRead)
{
    pw::VirtualDeviceState state;
    state.device.id = u"1a2b3c4d"_s;
    state.device.name = u"Every app"_s;
    state.status = pw::VirtualDeviceStatus::Present;

    const QJsonObject readable = parse(virtualDevicesMessage({state}, std::nullopt, std::nullopt));
    ASSERT_EQ(readable.value("devices"_L1).toArray().size(), 1);
    EXPECT_EQ(readable.value("devices"_L1).toArray().at(0).toObject().value("status"_L1).toString(), u"present"_s);
    EXPECT_TRUE(readable.value("unreadable"_L1).toObject().isEmpty());

    const QJsonObject broken =
        parse(virtualDevicesMessage({}, pw::VirtualDeviceError::NotOurs, pw::VirtualDeviceError::Malformed));
    EXPECT_EQ(broken.value("unreadable"_L1).toObject().value("user"_L1).toString(), u"NotOurs"_s);
    EXPECT_EQ(broken.value("unreadable"_L1).toObject().value("system"_L1).toString(), u"Malformed"_s);
}

TEST(VirtualDeviceFacade, EveryErrorIsAReasonAndBack)
{
    const QStringList reasons = virtualDeviceReasons();
    EXPECT_TRUE(reasons.contains(QString(kVirtualDeviceBadRequest)));
    // Every error the manager has is a reason the interface must have words
    // for: counted up to the last one, so a new error with no entry fails.
    for (int value = 0; value <= static_cast<int>(pw::VirtualDeviceError::ReadOnlyScope); ++value) {
        const auto error = static_cast<pw::VirtualDeviceError>(value);
        const QString reason = virtualDeviceReason(error);
        SCOPED_TRACE(reason.toStdString());
        EXPECT_TRUE(reasons.contains(reason));
        EXPECT_EQ(virtualDeviceErrorFromReason(reason), error);
    }
    EXPECT_FALSE(virtualDeviceErrorFromReason(u"SomethingElse"_s).has_value());
}

TEST_F(MockVirtualDevices, ACreatedDeviceIsAnsweredWithItsIdThenTheList)
{
    QStringList heard;
    QObject::connect(&engine, &EngineFacade::virtualDeviceCreated, [&heard] { heard.append(u"created"_s); });
    QObject::connect(&engine, &EngineFacade::virtualDevicesChanged, [&heard] { heard.append(u"list"_s); });

    const QString id = make(u"  Every app  "_s);
    ASSERT_EQ(errors.count(), 0);
    ASSERT_TRUE(pw::isValidVirtualDeviceId(id)) << id.toStdString();
    EXPECT_EQ(heard.mid(0, 2), (QStringList{u"created"_s, u"list"_s}));

    const QJsonObject listed = device(id);
    EXPECT_EQ(listed.value("name"_L1).toString(), u"Every app"_s) << "trimmed, like the real manager";
    EXPECT_EQ(listed.value("nodeName"_L1).toString(), u"reverblabr-virtual."_s + id);
    EXPECT_EQ(listed.value("direction"_L1).toString(), u"output"_s);
    EXPECT_EQ(listed.value("channels"_L1).toString(), u"stereo"_s);
    EXPECT_TRUE(listed.value("enabled"_L1).toBool());
    EXPECT_EQ(listed.value("scope"_L1).toString(), u"user"_s);
    EXPECT_EQ(listed.value("status"_L1).toString(), u"present"_s);
}

TEST_F(MockVirtualDevices, APresentDeviceIsInTheInventoryAsTheRealOneListsIt)
{
    const QString output = make(u"Every app"_s, u"output"_s);
    const QString microphone = make(u"Processed"_s, u"input"_s);

    // An output as an output, and through its monitor as an input.
    EXPECT_TRUE(inInventory(u"reverblabr-virtual."_s + output, "outputs"));
    EXPECT_TRUE(inInventory(u"reverblabr-virtual."_s + output, "inputs"));
    // A microphone as an input, and through the ports that feed it as an
    // output.
    EXPECT_TRUE(inInventory(u"reverblabr-virtual."_s + microphone, "inputs"));
    EXPECT_TRUE(inInventory(u"reverblabr-virtual."_s + microphone, "outputs"));

    // Switched off, it leaves the machine.
    engine.setVirtualDeviceEnabled(output, false);
    EXPECT_EQ(device(output).value("status"_L1).toString(), u"disabled"_s);
    EXPECT_FALSE(inInventory(u"reverblabr-virtual."_s + output, "outputs"));
    EXPECT_FALSE(inInventory(u"reverblabr-virtual."_s + output, "inputs"));
}

TEST_F(MockVirtualDevices, RenameSwitchAndDeleteAreEachAnsweredByOneList)
{
    const QString id = make(u"Every app"_s);

    lists.clear();
    engine.renameVirtualDevice(id, u"All the apps"_s);
    EXPECT_EQ(lists.count(), 1);
    EXPECT_EQ(device(id).value("name"_L1).toString(), u"All the apps"_s);

    // **Even a rename to the same name is answered**: a caller that asked
    // always hears back.
    lists.clear();
    engine.renameVirtualDevice(id, u"All the apps"_s);
    EXPECT_EQ(lists.count(), 1);

    lists.clear();
    engine.setVirtualDeviceEnabled(id, false);
    EXPECT_EQ(lists.count(), 1);
    EXPECT_FALSE(device(id).value("enabled"_L1).toBool());
    engine.setVirtualDeviceEnabled(id, true);
    EXPECT_EQ(device(id).value("status"_L1).toString(), u"present"_s);

    lists.clear();
    engine.deleteVirtualDevice(id);
    EXPECT_EQ(lists.count(), 1);
    EXPECT_TRUE(devices().isEmpty());
    EXPECT_EQ(errors.count(), 0);
}

TEST_F(MockVirtualDevices, ARefusalChangesNothingAndSaysWhyInTheEnginesWords)
{
    const QString id = make(u"Every app"_s);
    lists.clear();

    engine.createVirtualDevice(request(u"   "_s, u"output"_s));
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualNotCreated"_s, u"EmptyName"_s));
    engine.createVirtualDevice(request(u"Line\nbreak"_s, u"output"_s));
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualNotCreated"_s, u"NameHasControlCharacter"_s));
    engine.createVirtualDevice(request(QString(pw::kMaxVirtualDeviceNameLength + 1, u'x'), u"output"_s));
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualNotCreated"_s, u"NameTooLong"_s));
    engine.createVirtualDevice(request(u"Sideways"_s, u"sideways"_s));
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualNotCreated"_s, u"BadRequest"_s));

    engine.renameVirtualDevice(id, QString());
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualRefused"_s, u"EmptyName"_s));
    // The device the refusal is about is named by its name, not its id.
    EXPECT_EQ(parse(errors.back().at(1).toString()).value("device"_L1).toString(), u"Every app"_s);
    engine.deleteVirtualDevice(u"00000000"_s);
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualRefused"_s, u"NotFound"_s));

    EXPECT_EQ(lists.count(), 0) << "a refusal is not answered with a list: nothing changed";
    EXPECT_EQ(created.count(), 1);
    EXPECT_EQ(device(id).value("name"_L1).toString(), QString()) << "no list was sent since the create";
    engine.listVirtualDevices();
    EXPECT_EQ(devices().size(), 1);
    EXPECT_EQ(device(id).value("name"_L1).toString(), u"Every app"_s);
}

TEST_F(MockVirtualDevices, AnAllUsersDeviceIsReadOnly)
{
    const QString everyone =
        engine.addFakeVirtualDevice(uR"({"name": "Everyone's", "direction": "input", "scope": "system"})"_s);
    ASSERT_EQ(device(everyone).value("scope"_L1).toString(), u"system"_s);

    engine.renameVirtualDevice(everyone, u"Mine now"_s);
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualRefused"_s, u"ReadOnlyScope"_s));
    engine.setVirtualDeviceEnabled(everyone, false);
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualRefused"_s, u"ReadOnlyScope"_s));
    engine.deleteVirtualDevice(everyone);
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualRefused"_s, u"ReadOnlyScope"_s));
    EXPECT_EQ(device(everyone).value("name"_L1).toString(), u"Everyone's"_s);

    // The user's own come first, as the real manager lists them.
    const QString mine = make(u"Mine"_s);
    ASSERT_EQ(devices().size(), 2);
    EXPECT_EQ(devices().at(0).toObject().value("id"_L1).toString(), mine);
}

TEST_F(MockVirtualDevices, ATestCanRefuseTheNextChangeOnce)
{
    const QString id = make(u"Every app"_s);

    engine.refuseNextVirtualDeviceChange(u"FileNotWritable"_s);
    engine.renameVirtualDevice(id, u"Renamed"_s);
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualRefused"_s, u"FileNotWritable"_s));
    EXPECT_EQ(device(id).value("name"_L1).toString(), u"Every app"_s);

    engine.renameVirtualDevice(id, u"Renamed"_s);
    EXPECT_EQ(errors.count(), 1) << "one refusal, then back to normal";
    EXPECT_EQ(device(id).value("name"_L1).toString(), u"Renamed"_s);

    engine.refuseNextVirtualDeviceChange(u"NotOurs"_s);
    engine.createVirtualDevice(request(u"Another"_s, u"output"_s));
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualNotCreated"_s, u"NotOurs"_s));
    EXPECT_EQ(created.count(), 1);
}

TEST_F(MockVirtualDevices, AnUnreadableFileHidesItsScopeAndRefusesChanges)
{
    const QString id = make(u"Every app"_s);

    engine.setFakeVirtualDeviceFileUnreadable(u"user"_s, u"Malformed"_s);
    EXPECT_TRUE(devices().isEmpty());
    EXPECT_EQ(lastList().value("unreadable"_L1).toObject().value("user"_L1).toString(), u"Malformed"_s);
    EXPECT_FALSE(inInventory(u"reverblabr-virtual."_s + id, "outputs"));

    // The real manager will not write over a file it could not read.
    engine.createVirtualDevice(request(u"Another"_s, u"output"_s));
    EXPECT_EQ(lastError(), std::make_pair(u"device.virtualNotCreated"_s, u"Malformed"_s));

    engine.setFakeVirtualDeviceFileUnreadable(u"user"_s, QString());
    EXPECT_EQ(devices().size(), 1);
    EXPECT_TRUE(lastList().value("unreadable"_L1).toObject().isEmpty());
}

TEST_F(MockVirtualDevices, WhereADeviceStandsCanBeSetAndARefusedNodeIsReported)
{
    const QString id = make(u"Every app"_s);

    engine.setFakeVirtualDeviceStatus(id, u"creating"_s);
    EXPECT_EQ(device(id).value("status"_L1).toString(), u"creating"_s);
    EXPECT_FALSE(inInventory(u"reverblabr-virtual."_s + id, "outputs")) << "not in the graph yet";

    engine.refuseFakeVirtualDeviceNode(id);
    EXPECT_EQ(device(id).value("status"_L1).toString(), u"missing"_s);
    ASSERT_EQ(errors.count(), 1);
    EXPECT_EQ(errors.back().at(0).toString(), u"device.virtualNodeRefused"_s);
    EXPECT_EQ(parse(errors.back().at(1).toString()).value("device"_L1).toString(), u"Every app"_s);

    engine.setFakeVirtualDeviceStatus(id, u"present"_s);
    EXPECT_TRUE(inInventory(u"reverblabr-virtual."_s + id, "outputs"));
}

TEST_F(MockVirtualDevices, ClearingForgetsEveryDeviceAndEveryPretence)
{
    const QString own = make(u"Every app"_s);
    engine.addFakeVirtualDevice(u"{\"name\":\"Shared\",\"scope\":\"system\"}"_s);
    engine.setFakeVirtualDeviceFileUnreadable(u"system"_s, u"Malformed"_s);
    engine.refuseNextVirtualDeviceChange(u"FileNotWritable"_s);

    // **What a test's cleanup needs**: an all-users device cannot be
    // deleted through the facade, and a refusal left waiting would meet the
    // next test's first change.
    engine.clearFakeVirtualDevices();
    EXPECT_TRUE(devices().isEmpty());
    EXPECT_TRUE(lastList().value("unreadable"_L1).toObject().isEmpty());
    EXPECT_FALSE(inInventory(u"reverblabr-virtual."_s + own, "outputs"));

    const int before = static_cast<int>(errors.count());
    EXPECT_FALSE(make(u"Next"_s).isEmpty()) << "the waiting refusal outlived the clearing";
    EXPECT_EQ(errors.count(), before);
}

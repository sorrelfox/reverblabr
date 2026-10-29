// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Devices for all users: what the window's list sends the helper,
// and what each answer from the password prompt does to it. The helper here
// is a stand-in the test controls; what the real one does as root is
// tests/unit/helper, and the real prompt is a check on the desktop.

#include "core/mockengine.h"
#include "core/presetstore.h"
#include "core/virtualdevices.h"
#include "errormessages.h"
#include "pw/virtualdeviceconf.h"
#include "systemdevicewriter.h"
#include "virtualdevices.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <functional>
#include <optional>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// **No test in this binary ever reaches the system bus.** A model sends an
/// all-users change to the helper through KAuth unless told otherwise; in
/// place of that, every writer made here answers at once that no helper is
/// installed. The tests below give their own stand-in on top.
const bool kNoRealHelper = [] {
    ui::SystemDeviceWriter::setRunnerForTesting([](const QVariantMap &, QWindow *, ui::SystemDeviceWriter::Done done) {
        done(QString(ui::SystemDeviceWriter::kUnavailable));
    });
    return true;
}();

QCoreApplication &application()
{
    if (QCoreApplication *running = QCoreApplication::instance()) {
        return *running;
    }
    static int argc = 1;
    static char name[] = "reverblabr-app-test";
    static char *argv[] = {name, nullptr};
    static auto *const made = new QCoreApplication(argc, argv);
    return *made;
}

/// A model on its own mock, with a stand-in helper that keeps each request
/// until the test answers it.
class AllUsersDevices : public ::testing::Test
{
protected:
    AllUsersDevices()
    {
        application();
        model.attach(&engine);
        model.refresh();
        model.systemWriter()->setRunner(
            [this](const QVariantMap &arguments, QWindow *, ui::SystemDeviceWriter::Done done) {
                requests.append(arguments);
                pending = std::move(done);
            });
    }

    QTemporaryDir presets;
    core::MockEngine engine{core::PresetStore{presets.path()}};
    ui::VirtualDevices model;
    QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};

    QList<QVariantMap> requests;
    std::optional<ui::SystemDeviceWriter::Done> pending;

    /// Answers the request that is out the way the password prompt would.
    void answer(const QString &reason)
    {
        ASSERT_TRUE(pending.has_value());
        const ui::SystemDeviceWriter::Done done = *std::exchange(pending, std::nullopt);
        done(reason);
    }

    /// Answers it the way a helper that was let through does: the mock's
    /// all-users devices become the list it was asked to write.
    void write()
    {
        ASSERT_FALSE(requests.isEmpty());
        engine.writeFakeSystemVirtualDevices(requests.back().value(u"devices"_s).toList());
        answer(QString());
    }

    [[nodiscard]] QString addForAllUsers(const QString &name, bool enabled = true)
    {
        const QString id = engine.addFakeVirtualDevice(
            uR"({"name": "%1", "scope": "system", "enabled": %2})"_s.arg(name, enabled ? u"true"_s : u"false"_s));
        model.refresh();
        return id;
    }

    [[nodiscard]] QVariantMap row(const QString &deviceId) const
    {
        return model.get(model.indexOf(deviceId));
    }

    [[nodiscard]] static QList<pw::VirtualDevice> devicesIn(const QVariant &list)
    {
        const auto devices = pw::virtualDevicesFromVariant(list, pw::VirtualDeviceScope::System);
        return devices ? *devices : QList<pw::VirtualDevice>{};
    }
};

} // namespace

TEST_F(AllUsersDevices, TheBuildCanAskTheHelper)
{
    // This build has KAuth; one without says so, and the window offers no
    // all-users choice.
    EXPECT_EQ(model.allUsersAvailable(), ui::SystemDeviceWriter::available());
}

TEST_F(AllUsersDevices, ARenameAsksTheHelperWithTheWholeListAndTheOneItChanges)
{
    const QString mix = addForAllUsers(u"Mix"_s);
    const QString other = addForAllUsers(u"Other"_s, false);
    QSignalSpy authorizing(&model, &ui::VirtualDevices::authorizingChanged);

    model.rename(mix, u"  Broadcast mix "_s);

    ASSERT_EQ(requests.size(), 1);
    const QVariantMap arguments = requests.front();
    EXPECT_EQ(arguments.keys(), (QStringList{u"devices"_s, u"previous"_s}));
    const QList<pw::VirtualDevice> previous = devicesIn(arguments.value(u"previous"_s));
    const QList<pw::VirtualDevice> devices = devicesIn(arguments.value(u"devices"_s));
    ASSERT_EQ(previous.size(), 2);
    ASSERT_EQ(devices.size(), 2);
    EXPECT_EQ(previous.at(0).name, u"Mix"_s);
    EXPECT_EQ(devices.at(0).id, mix);
    // Trimmed, as the engine would keep it.
    EXPECT_EQ(devices.at(0).name, u"Broadcast mix"_s);
    EXPECT_EQ(devices.at(1), previous.at(1));
    EXPECT_EQ(devices.at(1).id, other);

    // Waiting for the password: the row is pending and keeps its name.
    EXPECT_TRUE(model.authorizing());
    EXPECT_EQ(authorizing.count(), 1);
    EXPECT_TRUE(row(mix).value(u"pending"_s).toBool());
    EXPECT_EQ(row(mix).value(u"name"_s).toString(), u"Mix"_s);

    write();
    EXPECT_FALSE(model.authorizing());
    EXPECT_EQ(row(mix).value(u"name"_s).toString(), u"Broadcast mix"_s);
    EXPECT_EQ(row(mix).value(u"scope"_s).toString(), u"system"_s);
    EXPECT_FALSE(row(mix).value(u"pending"_s).toBool());
}

TEST_F(AllUsersDevices, SwitchingOffAndDeletingGoThroughTheHelperToo)
{
    const QString mix = addForAllUsers(u"Mix"_s);

    model.setEnabled(mix, false);
    ASSERT_EQ(requests.size(), 1);
    EXPECT_FALSE(devicesIn(requests.back().value(u"devices"_s)).front().enabled);
    write();
    EXPECT_FALSE(row(mix).value(u"enabled"_s).toBool());
    EXPECT_EQ(row(mix).value(u"status"_s).toString(), u"disabled"_s);

    model.remove(mix);
    ASSERT_EQ(requests.size(), 2);
    EXPECT_TRUE(devicesIn(requests.back().value(u"devices"_s)).isEmpty());
    write();
    EXPECT_EQ(model.indexOf(mix), -1);
}

TEST_F(AllUsersDevices, ANewDeviceIsCreatedWithAnIdNeitherFileUses)
{
    const QString mine = engine.addFakeVirtualDevice(uR"({"name": "Mine"})"_s);
    const QString theirs = addForAllUsers(u"Theirs"_s);
    QSignalSpy created(&model, &ui::VirtualDevices::created);

    model.createForAllUsers(u"Studio"_s, u"input"_s, u"mono"_s);
    EXPECT_TRUE(model.creating());
    ASSERT_EQ(requests.size(), 1);
    const QList<pw::VirtualDevice> devices = devicesIn(requests.back().value(u"devices"_s));
    ASSERT_EQ(devices.size(), 2);
    const pw::VirtualDevice made = devices.back();
    EXPECT_NE(made.id, mine);
    EXPECT_NE(made.id, theirs);
    EXPECT_EQ(made.name, u"Studio"_s);
    EXPECT_EQ(made.direction, pw::VirtualDeviceDirection::Input);
    EXPECT_EQ(made.channels, pw::VirtualDeviceChannels::Mono);
    EXPECT_TRUE(made.enabled);

    write();
    ASSERT_EQ(created.count(), 1);
    EXPECT_EQ(created.front().at(0).toString(), made.id);
    EXPECT_FALSE(model.creating());
    EXPECT_EQ(row(made.id).value(u"scope"_s).toString(), u"system"_s);
    EXPECT_EQ(row(made.id).value(u"direction"_s).toString(), u"input"_s);
}

TEST_F(AllUsersDevices, ACancelledOrRefusedPasswordChangesNothingAndSaysSo)
{
    const QString mix = addForAllUsers(u"Mix"_s);
    for (const QString &reason : {QString(ui::SystemDeviceWriter::kCancelled),
                                  QString(ui::SystemDeviceWriter::kDenied),
                                  QString(ui::SystemDeviceWriter::kUnavailable)}) {
        SCOPED_TRACE(reason.toStdString());
        QSignalSpy refused(&model, &ui::VirtualDevices::refused);
        QSignalSpy lists(&engine, &core::EngineFacade::virtualDevicesChanged);

        model.rename(mix, u"Broadcast mix"_s);
        answer(reason);

        ASSERT_EQ(refused.count(), 1);
        EXPECT_EQ(refused.front().at(0).toString(), u"device.virtualRefused"_s);
        EXPECT_EQ(refused.front().at(1).toString(), reason);
        const QString message = refused.front().at(2).toString();
        EXPECT_TRUE(message.contains(u"Mix"_s)) << message.toStdString();
        EXPECT_TRUE(message.contains(app::reasonMessage(reason))) << message.toStdString();
        // Nothing was written, so nothing is asked of the engine.
        EXPECT_EQ(lists.count(), 0);
        EXPECT_EQ(row(mix).value(u"name"_s).toString(), u"Mix"_s);
        EXPECT_FALSE(row(mix).value(u"pending"_s).toBool());
        EXPECT_FALSE(model.authorizing());
    }

    // A refused create is a create's refusal.
    QSignalSpy refused(&model, &ui::VirtualDevices::refused);
    model.createForAllUsers(u"Studio"_s, u"output"_s, u"stereo"_s);
    answer(QString(ui::SystemDeviceWriter::kCancelled));
    ASSERT_EQ(refused.count(), 1);
    EXPECT_EQ(refused.front().at(0).toString(), u"device.virtualNotCreated"_s);
    EXPECT_TRUE(refused.front().at(2).toString().contains(u"Studio"_s));
    EXPECT_FALSE(model.creating());
    EXPECT_EQ(model.count(), 1);
}

TEST_F(AllUsersDevices, AListThatNoLongerMatchesIsReloadedSoTheNextTryStartsFromIt)
{
    const QString mix = addForAllUsers(u"Mix"_s);
    model.rename(mix, u"Broadcast mix"_s);

    // Meanwhile another user added one; the helper refuses.
    engine.writeFakeSystemVirtualDevices(
        pw::virtualDevicesToVariant({devicesIn(requests.back().value(u"previous"_s)).front(),
                                     pw::VirtualDevice{u"feedf00d"_s,
                                                       u"Theirs"_s,
                                                       pw::VirtualDeviceDirection::Output,
                                                       pw::VirtualDeviceChannels::Stereo,
                                                       true,
                                                       pw::VirtualDeviceScope::System}}));
    QSignalSpy refused(&model, &ui::VirtualDevices::refused);
    answer(QString(core::kVirtualDeviceChangedElsewhere));

    ASSERT_EQ(refused.count(), 1);
    EXPECT_EQ(refused.front().at(1).toString(), QString(core::kVirtualDeviceChangedElsewhere));
    EXPECT_EQ(row(mix).value(u"name"_s).toString(), u"Mix"_s);
    EXPECT_GE(model.indexOf(u"feedf00d"_s), 0);
    EXPECT_FALSE(row(mix).value(u"pending"_s).toBool());
}

TEST_F(AllUsersDevices, OneChangeAtATime)
{
    const QString mix = addForAllUsers(u"Mix"_s);
    model.rename(mix, u"First"_s);
    QSignalSpy refused(&model, &ui::VirtualDevices::refused);

    model.setEnabled(mix, false);
    model.createForAllUsers(u"Second"_s, u"output"_s, u"stereo"_s);

    EXPECT_EQ(requests.size(), 1);
    ASSERT_EQ(refused.count(), 2);
    EXPECT_EQ(refused.at(0).at(1).toString(), QString(ui::VirtualDevices::kAuthorizationPending));
    EXPECT_EQ(refused.at(1).at(1).toString(), QString(ui::VirtualDevices::kAuthorizationPending));
    write();
    EXPECT_EQ(row(mix).value(u"name"_s).toString(), u"First"_s);
    EXPECT_TRUE(row(mix).value(u"enabled"_s).toBool());
}

TEST_F(AllUsersDevices, NothingIsAskedWhenTheChangeCannotBeMade)
{
    const QString mix = addForAllUsers(u"Mix"_s);
    QSignalSpy refused(&model, &ui::VirtualDevices::refused);

    model.rename(mix, u"   "_s);
    model.createForAllUsers(u"Line\nbreak"_s, u"output"_s, u"stereo"_s);
    model.createForAllUsers(u"Sideways"_s, u"sideways"_s, u"stereo"_s);
    // The system file could not be read: its devices are not known, so a
    // list without them would lose them.
    engine.setFakeVirtualDeviceFileUnreadable(u"system"_s, u"NotOurs"_s);
    model.createForAllUsers(u"Studio"_s, u"output"_s, u"stereo"_s);

    EXPECT_TRUE(requests.isEmpty());
    ASSERT_EQ(refused.count(), 4);
    EXPECT_EQ(refused.at(0).at(1).toString(), u"EmptyName"_s);
    EXPECT_EQ(refused.at(1).at(1).toString(), u"NameHasControlCharacter"_s);
    EXPECT_EQ(refused.at(2).at(1).toString(), u"BadRequest"_s);
    EXPECT_EQ(refused.at(3).at(1).toString(), u"NotOurs"_s);
    EXPECT_FALSE(model.authorizing());
}

TEST_F(AllUsersDevices, TheUsersOwnDevicesNeverGoNearTheHelper)
{
    const QString mine = engine.addFakeVirtualDevice(uR"({"name": "Mine"})"_s);
    model.refresh();

    model.rename(mine, u"Still mine"_s);
    model.setEnabled(mine, false);
    model.create(u"Another"_s, u"output"_s, u"stereo"_s);
    model.remove(mine);

    EXPECT_TRUE(requests.isEmpty());
    EXPECT_EQ(model.indexOf(mine), -1);
    EXPECT_EQ(model.count(), 1);
}

TEST(SystemDeviceWriterTest, AnswersOnceAndTakesOneRequestAtATime)
{
    application();
    ui::SystemDeviceWriter writer;
    QList<ui::SystemDeviceWriter::Done> outstanding;
    writer.setRunner([&outstanding](const QVariantMap &, QWindow *, ui::SystemDeviceWriter::Done done) {
        outstanding.append(std::move(done));
    });
    QSignalSpy finished(&writer, &ui::SystemDeviceWriter::finished);
    QSignalSpy busy(&writer, &ui::SystemDeviceWriter::busyChanged);

    EXPECT_TRUE(writer.write({}));
    EXPECT_TRUE(writer.busy());
    EXPECT_FALSE(writer.write({}));
    ASSERT_EQ(outstanding.size(), 1);

    outstanding.front()(QString(ui::SystemDeviceWriter::kDenied));
    outstanding.front()(QString());
    ASSERT_EQ(finished.count(), 1);
    EXPECT_EQ(finished.front().at(0).toString(), QString(ui::SystemDeviceWriter::kDenied));
    EXPECT_FALSE(writer.busy());
    EXPECT_EQ(busy.count(), 2);
}

TEST(SystemDeviceWriterTest, AnAnswerAfterTheWriterIsGoneIsDropped)
{
    application();
    ui::SystemDeviceWriter::Done late;
    {
        ui::SystemDeviceWriter writer;
        writer.setRunner(
            [&late](const QVariantMap &, QWindow *, ui::SystemDeviceWriter::Done done) { late = std::move(done); });
        ASSERT_TRUE(writer.write({}));
    }
    ASSERT_TRUE(late);
    late(QString());
}

TEST(SystemDeviceWriterTest, EveryReasonOfItsOwnHasWords)
{
    QStringList reasons = ui::SystemDeviceWriter::reasons();
    reasons.append(QString(ui::VirtualDevices::kAuthorizationPending));
    for (const QString &reason : reasons) {
        SCOPED_TRACE(reason.toStdString());
        const QString words = app::reasonMessage(reason);
        EXPECT_FALSE(words.isEmpty());
        EXPECT_NE(words, reason);
    }
}

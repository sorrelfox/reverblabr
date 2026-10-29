// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The virtual devices as the window lists them: the rows follow what
// the engine says, one change at a time and never by starting over, and a
// refusal reaches the window with the words for it.

#include "core/mockengine.h"
#include "core/presetstore.h"
#include "virtualdevicefilter.h"
#include "virtualdevices.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace rb;
using namespace Qt::StringLiterals;

namespace
{

/// A model following its own mock, which has been asked for its list.
class VirtualDevicesModel : public ::testing::Test
{
protected:
    VirtualDevicesModel()
    {
        model.attach(&engine);
        model.refresh();
    }

    QTemporaryDir presets;
    core::MockEngine engine{core::PresetStore{presets.path()}};
    ui::VirtualDevices model;
    /// Qt's own check that every signal the model sends is one a view can
    /// follow: rows inserted where they are said to be, moves that add up.
    QAbstractItemModelTester tester{&model, QAbstractItemModelTester::FailureReportingMode::Fatal};

    [[nodiscard]] QVariant role(int row, ui::VirtualDevices::Role role) const
    {
        return model.data(model.index(row), role);
    }

    /// The id of the device just made through the model.
    [[nodiscard]] QString make(const QString &name, const QString &direction = u"output"_s)
    {
        QSignalSpy created(&model, &ui::VirtualDevices::created);
        model.create(name, direction, u"stereo"_s);
        return created.isEmpty() ? QString() : created.back().at(0).toString();
    }
};

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

} // namespace

TEST(VirtualDevicesTest, IsNotReadyUntilTheEngineHasAnsweredAndAsksOfItsOwnAccord)
{
    application();
    QTemporaryDir presets;
    core::MockEngine engine{core::PresetStore{presets.path()}};
    const QString id = engine.addFakeVirtualDevice(uR"({"name": "Already there"})"_s);

    ui::VirtualDevices model;
    model.attach(&engine);
    // An empty list before the engine has answered means "not known".
    EXPECT_FALSE(model.ready());
    EXPECT_EQ(model.count(), 0);

    // **Asked after attaching returns, never during it**.
    QCoreApplication::processEvents();
    EXPECT_TRUE(model.ready());
    ASSERT_EQ(model.count(), 1);
    EXPECT_EQ(model.get(0).value(u"deviceId"_s).toString(), id);
}

TEST_F(VirtualDevicesModel, EveryRoleComesFromTheEngine)
{
    ASSERT_TRUE(model.ready());
    const QString id = make(u"Every app"_s);
    ASSERT_EQ(model.count(), 1);

    EXPECT_EQ(role(0, ui::VirtualDevices::IdRole).toString(), id);
    EXPECT_EQ(role(0, ui::VirtualDevices::NameRole).toString(), u"Every app"_s);
    EXPECT_EQ(role(0, ui::VirtualDevices::NodeNameRole).toString(), u"reverblabr-virtual."_s + id);
    EXPECT_EQ(role(0, ui::VirtualDevices::DirectionRole).toString(), u"output"_s);
    EXPECT_EQ(role(0, ui::VirtualDevices::ChannelsRole).toString(), u"stereo"_s);
    EXPECT_TRUE(role(0, ui::VirtualDevices::EnabledRole).toBool());
    EXPECT_EQ(role(0, ui::VirtualDevices::ScopeRole).toString(), u"user"_s);
    EXPECT_EQ(role(0, ui::VirtualDevices::StatusRole).toString(), u"present"_s);
    EXPECT_FALSE(role(0, ui::VirtualDevices::PendingRole).toBool());

    // QML reads them by these names.
    const QHash<int, QByteArray> names = model.roleNames();
    for (const char *name :
         {"deviceId", "name", "nodeName", "direction", "channels", "enabled", "scope", "status", "pending"}) {
        EXPECT_TRUE(names.values().contains(QByteArray(name))) << name;
    }
    EXPECT_EQ(model.maximumNameLength(), 64);
}

TEST_F(VirtualDevicesModel, RowsChangeInPlaceAndAreNeverReset)
{
    const QString first = make(u"First"_s);
    const QString second = make(u"Second"_s);
    ASSERT_EQ(model.count(), 2);

    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    const QPersistentModelIndex kept(model.index(model.indexOf(second)));

    // Where one device stands changes: one row changes, and a view's hold
    // on the other survives.
    engine.setFakeVirtualDeviceStatus(first, u"missing"_s);
    EXPECT_EQ(role(model.indexOf(first), ui::VirtualDevices::StatusRole).toString(), u"missing"_s);
    EXPECT_GE(changed.count(), 1);
    for (const QList<QVariant> &signal : changed) {
        EXPECT_EQ(signal.at(0).toModelIndex().row(), model.indexOf(first));
    }
    model.rename(first, u"First, renamed"_s);
    model.setEnabled(second, false);
    EXPECT_EQ(role(model.indexOf(first), ui::VirtualDevices::NameRole).toString(), u"First, renamed"_s);
    EXPECT_FALSE(role(model.indexOf(second), ui::VirtualDevices::EnabledRole).toBool());

    EXPECT_EQ(resets.count(), 0);
    ASSERT_TRUE(kept.isValid());
    EXPECT_EQ(kept.data(ui::VirtualDevices::IdRole).toString(), second);

    // Deleting one removes that row and no other.
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    model.remove(first);
    EXPECT_EQ(removed.count(), 1);
    EXPECT_EQ(model.count(), 1);
    EXPECT_TRUE(kept.isValid());
    EXPECT_EQ(resets.count(), 0);
}

TEST_F(VirtualDevicesModel, TheOrderFollowsTheEngine)
{
    const QString everyone = engine.addFakeVirtualDevice(uR"({"name": "Everyone's", "scope": "system"})"_s);
    const QString mine = make(u"Mine"_s);
    // The user's own come first, as the engine lists them, even though this
    // one arrived second.
    ASSERT_EQ(model.count(), 2);
    EXPECT_EQ(model.indexOf(mine), 0);
    EXPECT_EQ(model.indexOf(everyone), 1);
    EXPECT_EQ(model.get(1).value(u"scope"_s).toString(), u"system"_s);
}

TEST_F(VirtualDevicesModel, ACallIsPendingUntilItIsAnswered)
{
    const QString id = make(u"Every app"_s);
    EXPECT_FALSE(model.creating());

    // The mock answers inside the call, so what a row looks like while it
    // waits is read each time the model says the pending marks moved: marked
    // when asked, clear once answered.
    QList<bool> marks;
    QObject::connect(&model, &ui::VirtualDevices::pendingChanged, [&] {
        marks.append(role(model.indexOf(id), ui::VirtualDevices::PendingRole).toBool());
    });
    model.rename(id, u"Renamed"_s);
    EXPECT_EQ(marks, (QList<bool>{true, false}));

    // A refusal answers a call just as a list does.
    marks.clear();
    engine.refuseNextVirtualDeviceChange(u"FileNotWritable"_s);
    model.setEnabled(id, false);
    EXPECT_EQ(marks, (QList<bool>{true, false}));

    QList<bool> creating;
    QObject::connect(&model, &ui::VirtualDevices::pendingChanged, [&] { creating.append(model.creating()); });
    engine.refuseNextVirtualDeviceChange(u"FileNotWritable"_s);
    model.create(u"Another"_s, u"output"_s, u"mono"_s);
    EXPECT_EQ(creating, (QList<bool>{true, false}));
    EXPECT_FALSE(model.creating());
}

TEST_F(VirtualDevicesModel, ARefusalReachesTheWindowWithItsWordsAndChangesNothing)
{
    const QString id = make(u"Every app"_s);
    QSignalSpy refused(&model, &ui::VirtualDevices::refused);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    model.rename(id, u"   "_s);
    ASSERT_EQ(refused.count(), 1);
    EXPECT_EQ(refused.back().at(0).toString(), u"device.virtualRefused"_s);
    EXPECT_EQ(refused.back().at(1).toString(), u"EmptyName"_s);
    const QString message = refused.back().at(2).toString();
    EXPECT_TRUE(message.contains(u"Every app"_s)) << message.toStdString();
    EXPECT_FALSE(message.contains(u"EmptyName"_s)) << "the engine's word became ours: " << message.toStdString();
    EXPECT_EQ(role(0, ui::VirtualDevices::NameRole).toString(), u"Every app"_s);

    // An all-users device goes to the helper and its password prompt
    // rather than the engine; none is installed for these tests.
    const QString everyone = engine.addFakeVirtualDevice(uR"({"name": "Everyone's", "scope": "system"})"_s);
    model.remove(everyone);
    EXPECT_EQ(refused.back().at(1).toString(), u"HelperUnavailable"_s);
    EXPECT_NE(model.indexOf(everyone), -1);

    model.create(u"Sideways"_s, u"sideways"_s, u"stereo"_s);
    EXPECT_EQ(refused.back().at(0).toString(), u"device.virtualNotCreated"_s);
    EXPECT_EQ(refused.back().at(1).toString(), u"BadRequest"_s);

    // A node PipeWire would not make is reported too, with no reason.
    engine.refuseFakeVirtualDeviceNode(id);
    EXPECT_EQ(refused.back().at(0).toString(), u"device.virtualNodeRefused"_s);
    EXPECT_TRUE(refused.back().at(1).toString().isEmpty());
    EXPECT_EQ(role(model.indexOf(id), ui::VirtualDevices::StatusRole).toString(), u"missing"_s);

    // Other errors are not the window's.
    const qsizetype before = refused.count();
    engine.reportError(u"graph.cycle"_s, u"{}"_s);
    EXPECT_EQ(refused.count(), before);
}

TEST_F(VirtualDevicesModel, AnUnreadableFileIsSaidAndItsDevicesAreNotListed)
{
    ASSERT_FALSE(make(u"Every app"_s).isEmpty());
    QSignalSpy unreadable(&model, &ui::VirtualDevices::unreadableChanged);

    engine.setFakeVirtualDeviceFileUnreadable(u"user"_s, u"NotOurs"_s);
    EXPECT_EQ(unreadable.count(), 1);
    EXPECT_EQ(model.unreadable().value(u"user"_s).toString(), u"NotOurs"_s);
    EXPECT_EQ(model.count(), 0);
    const QString words = model.reasonText(u"NotOurs"_s);
    EXPECT_FALSE(words.isEmpty());
    EXPECT_NE(words, u"NotOurs"_s);

    engine.setFakeVirtualDeviceFileUnreadable(u"user"_s, QString());
    EXPECT_TRUE(model.unreadable().isEmpty());
    EXPECT_EQ(model.count(), 1);
}

TEST_F(VirtualDevicesModel, ANewDeviceIsInsertedWhereTheEngineListsIt)
{
    const QString everyone = engine.addFakeVirtualDevice(uR"({"name": "Everyone's", "scope": "system"})"_s);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy count(&model, &ui::VirtualDevices::countChanged);
    const QString mine = make(u"Mine"_s);
    ASSERT_EQ(inserted.count(), 1);
    EXPECT_EQ(inserted.back().at(1).toInt(), 0) << "the user's own go first";
    EXPECT_EQ(count.count(), 1);
    EXPECT_EQ(model.indexOf(mine), 0);
    EXPECT_EQ(model.indexOf(everyone), 1);
}

TEST_F(VirtualDevicesModel, EachKindIsListedOnItsOwnInTheEnginesOrder)
{
    application();
    // **The engine mixes the kinds**, the user's own first, then every
    // user's; the window lists each kind on its own.
    const QString first = make(u"Every app"_s, u"output"_s);
    const QString microphone = make(u"Processed voice"_s, u"input"_s);
    const QString second = make(u"Music only"_s, u"output"_s);

    ui::VirtualDeviceFilter outputs;
    outputs.setSourceModel(&model);
    outputs.setDirection(u"output"_s);
    QAbstractItemModelTester outputTester{&outputs, QAbstractItemModelTester::FailureReportingMode::Fatal};
    ui::VirtualDeviceFilter inputs;
    inputs.setSourceModel(&model);
    inputs.setDirection(u"input"_s);

    ASSERT_EQ(outputs.count(), 2);
    EXPECT_EQ(outputs.rowOf(first) >= 0 && outputs.rowOf(second) >= 0, true);
    EXPECT_EQ(outputs.rowOf(microphone), -1);
    ASSERT_EQ(inputs.count(), 1);
    EXPECT_EQ(inputs.rowOf(microphone), 0);
    EXPECT_EQ(inputs.data(inputs.index(0, 0), ui::VirtualDevices::NameRole).toString(), u"Processed voice"_s);

    // **A row that changes changes in place**, and a new or deleted device
    // is counted at once.
    QSignalSpy counted(&outputs, &ui::VirtualDeviceFilter::countChanged);
    model.rename(first, u"All the apps"_s);
    EXPECT_EQ(outputs.data(outputs.index(outputs.rowOf(first), 0), ui::VirtualDevices::NameRole).toString(),
              u"All the apps"_s);
    EXPECT_EQ(counted.count(), 0);

    const QString third = make(u"Games"_s, u"output"_s);
    EXPECT_EQ(outputs.count(), 3);
    EXPECT_GE(outputs.rowOf(third), 0);
    EXPECT_GE(counted.count(), 1);
    model.remove(first);
    EXPECT_EQ(outputs.count(), 2);
    EXPECT_EQ(outputs.rowOf(first), -1);
    EXPECT_EQ(inputs.count(), 1);
}

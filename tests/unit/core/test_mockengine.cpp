// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The mock through the flow an interface actually walks: make a session,
// build a chain in it, run it, stop it. It has to be convincing about the
// awkward parts — numbered patches, refusals, codes rather than text — or an
// interface written against it finds out the hard way.

#include "blocks/outputdevice.h"
#include "core/audiosystem.h"
#include "core/editcommand.h"
#include "core/mockengine.h"
#include "core/presetstore.h"
#include "core/session.h"
#include "core/telemetry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <vector>

using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString addBlockEdit(const QString &id, const QString &type)
{
    BlockInstance block;
    block.id = id;
    block.type = type;
    AddBlock command;
    command.block = block;
    return asJson(editCommandToJson(command));
}

/// A parameter value as the interface sends it: **wrapped in an array**,
/// because Qt will not parse a bare number or a bare string as a document.
[[nodiscard]] QString paramJson(const QJsonValue &value)
{
    return QString::fromUtf8(QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString connectEdit(const QString &from, const QString &to)
{
    Wire wire;
    wire.fromBlock = from;
    wire.toBlock = to;
    return asJson(editCommandToJson(Connect{wire, -1}));
}

/// A link, as the interface sends one: **a list of commands**, one a
/// channel, because a link carries a stereo signal and what a person drew
/// was one thing.
[[nodiscard]] QString connectLinkEdit(const QString &from, const QString &to, std::uint32_t channels)
{
    QJsonArray commands;
    for (std::uint32_t channel = 0; channel < channels; ++channel) {
        Wire wire;
        wire.fromBlock = from;
        wire.fromPort = channel;
        wire.toBlock = to;
        wire.toPort = channel;
        commands.append(editCommandToJson(Connect{wire, -1}));
    }
    return QString::fromUtf8(QJsonDocument(commands).toJson(QJsonDocument::Compact));
}

/// Watches everything the facade can say, so a test can ask what happened
/// without wiring up eight spies each time.
class Listener
{
public:
    explicit Listener(EngineFacade &facade)
        : opened(&facade, &EngineFacade::sessionOpened)
        , closed(&facade, &EngineFacade::sessionClosed)
        , snapshots(&facade, &EngineFacade::sessionSnapshot)
        , patches(&facade, &EngineFacade::sessionPatch)
        , states(&facade, &EngineFacade::stateChanged)
        , inventory(&facade, &EngineFacade::inventoryChanged)
        , errors(&facade, &EngineFacade::error)
        , listed(&facade, &EngineFacade::sessionsListed)
        , undoState(&facade, &EngineFacade::undoStateChanged)
        , openUrl(&facade, &EngineFacade::openUrlRequested)
        , presets(&facade, &EngineFacade::presetsListed)
    {
    }

    QSignalSpy opened;
    QSignalSpy closed;
    QSignalSpy snapshots;
    QSignalSpy patches;
    QSignalSpy states;
    QSignalSpy inventory;
    QSignalSpy errors;
    QSignalSpy listed;
    QSignalSpy undoState;
    QSignalSpy openUrl;
    QSignalSpy presets;

    /// The last thing said about undo: can undo, its code, can redo, its
    /// code.
    [[nodiscard]] QList<QVariant> lastUndoState() const
    {
        return undoState.isEmpty() ? QList<QVariant>{} : undoState.back();
    }

    [[nodiscard]] QString lastErrorCode() const
    {
        return errors.isEmpty() ? QString{} : errors.back().at(0).toString();
    }

    /// The "reason" argument of the last error, which is the stable name of
    /// what was wrong rather than any words about it.
    [[nodiscard]] QString lastErrorReason() const
    {
        if (errors.isEmpty()) {
            return {};
        }
        const QJsonDocument args = QJsonDocument::fromJson(errors.back().at(1).toString().toUtf8());
        return args.object().value(u"reason"_s).toString();
    }
};

} // namespace

TEST(MockEngineTest, WalksTheWholeFlowWithNoPipeWireAnywhere)
{
    MockEngine engine;
    Listener heard(engine);

    engine.createSession(u"Podcast"_s);
    ASSERT_EQ(heard.opened.count(), 1);
    const QString id = heard.opened.back().at(0).toString();
    ASSERT_FALSE(id.isEmpty());
    ASSERT_EQ(heard.snapshots.count(), 1);

    // source -> gain -> output, one edit at a time, as an interface would.
    engine.applyEdit(id, addBlockEdit(u"src"_s, u"source.application"_s));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, u"effect.volume"_s));
    engine.applyEdit(id, addBlockEdit(u"out"_s, u"output.device"_s));
    engine.applyEdit(id, connectEdit(u"src"_s, u"fx"_s));
    engine.applyEdit(id, connectEdit(u"fx"_s, u"out"_s));
    EXPECT_TRUE(heard.errors.isEmpty()) << heard.lastErrorCode().toStdString();
    ASSERT_EQ(heard.patches.count(), 5);

    engine.start(id);
    ASSERT_GE(heard.states.count(), 2);
    EXPECT_EQ(heard.states.back().at(1).toString(), u"running"_s);

    engine.stop(id);
    EXPECT_EQ(heard.states.back().at(1).toString(), u"idle"_s);

    engine.closeSession(id);
    EXPECT_EQ(heard.closed.count(), 1);
}

TEST(MockEngineTest, NumbersItsPatchesSoAGapCanBeSeen)
{
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Numbered"_s);
    const QString id = heard.opened.back().at(0).toString();
    const quint64 first = heard.snapshots.back().at(1).toULongLong();

    for (int edit = 0; edit < 4; ++edit) {
        engine.applyEdit(id, addBlockEdit(u"b%1"_s.arg(edit), u"effect.volume"_s));
    }
    ASSERT_EQ(heard.patches.count(), 4);

    quint64 previous = first;
    for (int patch = 0; patch < heard.patches.count(); ++patch) {
        const quint64 revision = heard.patches.at(patch).at(1).toULongLong();
        EXPECT_EQ(revision, previous + 1) << "patch " << patch << " does not follow the one before it";
        previous = revision;
    }

    // And asking again gives the whole thing at the revision reached.
    engine.resynchronise(id);
    ASSERT_EQ(heard.snapshots.count(), 2);
    EXPECT_EQ(heard.snapshots.back().at(1).toULongLong(), previous);
    const QJsonDocument snapshot = QJsonDocument::fromJson(heard.snapshots.back().at(2).toString().toUtf8());
    EXPECT_EQ(snapshot.object().value(u"blocks"_s).toArray().size(), 4);
}

TEST(MockEngineTest, RefusesAnEditTheRealEngineWouldRefuse)
{
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Refusals"_s);
    const QString id = heard.opened.back().at(0).toString();

    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    ASSERT_EQ(heard.patches.count(), 1);

    // The same id twice.
    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    EXPECT_EQ(heard.patches.count(), 1) << "a refused edit must not produce a patch";
    EXPECT_EQ(heard.lastErrorCode(), u"edit.refused"_s);

    // A wire to a block that is not there.
    engine.applyEdit(id, connectEdit(u"b1"_s, u"nowhere"_s));
    EXPECT_EQ(heard.patches.count(), 1);
    EXPECT_EQ(heard.lastErrorCode(), u"edit.refused"_s);

    // Something that is not an edit at all.
    engine.applyEdit(id, u"{\"op\": \"teleport\"}"_s);
    EXPECT_EQ(heard.lastErrorCode(), u"edit.refused"_s);

    // A session that is not open.
    engine.applyEdit(u"00000000-0000-0000-0000-000000000000"_s, addBlockEdit(u"b2"_s, u"effect.volume"_s));
    EXPECT_EQ(heard.lastErrorCode(), u"session.notFound"_s);
}

TEST(MockEngineTest, AParameterValueThatWillNotParseIsRefusedRatherThanStoredAsNull)
{
    // **Found while testing autosave**. A single value arrives wrapped
    // in an array, because Qt will not parse a bare number or a bare string
    // as a document. What arrived unwrapped used to be read with
    // `QJsonValue::fromVariant(document.toVariant())`, and a variant that
    // holds nothing becomes **null** rather than undefined — so a value that
    // could not be parsed passed the guard and was written into the session
    // as a deliberate null, which a block would then be asked to take.
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Values"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"effect.volume"_s));
    engine.resynchronise(id);
    const int snapshots = heard.snapshots.count();

    for (const QString &bad : {u"0.5"_s, u"true"_s, u"\"loud\""_s, u"not json at all"_s, QString{}}) {
        engine.setParam(id, u"b1"_s, u"gain"_s, bad);
        EXPECT_EQ(heard.lastErrorCode(), u"edit.refused"_s) << bad.toStdString();
        EXPECT_EQ(heard.lastErrorReason(), u"malformedValue"_s) << bad.toStdString();
    }

    // Nothing of it reached the session.
    engine.resynchronise(id);
    ASSERT_EQ(heard.snapshots.count(), snapshots + 1);
    const QJsonDocument snapshot = QJsonDocument::fromJson(heard.snapshots.back().at(2).toString().toUtf8());
    const QJsonObject block = snapshot.object().value(u"blocks"_s).toArray().at(0).toObject();
    EXPECT_FALSE(block.value(u"params"_s).toObject().contains(u"gain"_s))
        << "a value that would not parse was written into the session";

    // And the way the interface really sends one is taken.
    engine.setParam(id, u"b1"_s, u"gain"_s, paramJson(0.5));
    engine.resynchronise(id);
    const QJsonDocument after = QJsonDocument::fromJson(heard.snapshots.back().at(2).toString().toUtf8());
    const QJsonObject set = after.object().value(u"blocks"_s).toArray().at(0).toObject();
    EXPECT_DOUBLE_EQ(set.value(u"params"_s).toObject().value(u"gain"_s).toDouble(), 0.5);
}

TEST(MockEngineTest, RefusesAWireToAPortTheAudioSystemOwns)
{
    // A capture block's inputs are filled by the application or the
    // device it follows, and an output block's outputs belong to the device.
    // The canvas has always refused to draw such a wire, but a session
    // file written by hand, a session from another version or a second
    // client over the facade goes through none of the canvas — so the engine
    // refuses the edit itself, by the same name the interface spells out.
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"AudioSystemPorts"_s);
    const QString id = heard.opened.back().at(0).toString();

    engine.applyEdit(id, addBlockEdit(u"src"_s, u"source.device"_s));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, u"effect.volume"_s));
    engine.applyEdit(id, addBlockEdit(u"out"_s, u"output.device"_s));
    const int built = heard.patches.count();
    ASSERT_EQ(built, 3);

    engine.applyEdit(id, connectEdit(u"fx"_s, u"src"_s));
    EXPECT_EQ(heard.patches.count(), built) << "a refused wire must not produce a patch";
    EXPECT_EQ(heard.lastErrorCode(), u"edit.refused"_s);
    EXPECT_EQ(heard.lastErrorReason(), u"wireIntoSource"_s);

    engine.applyEdit(id, connectEdit(u"out"_s, u"fx"_s));
    EXPECT_EQ(heard.patches.count(), built);
    EXPECT_EQ(heard.lastErrorReason(), u"wireOutOfOutput"_s);

    // **And the way round a session is really built is taken**, so this is
    // a rule about which ports are wiring and not about wiring at all.
    engine.applyEdit(id, connectEdit(u"src"_s, u"fx"_s));
    engine.applyEdit(id, connectEdit(u"fx"_s, u"out"_s));
    EXPECT_EQ(heard.patches.count(), built + 2);
}

TEST(MockEngineTest, APresetIsAppliedAsOneEditAndUndoneAsOne)
{
    // What presets need from undo, through the facade the window uses.
    // **A preset store of its own**, so this neither reads what somebody
    // already saved nor leaves anything for the next run to trip over.
    QTemporaryDir presets;
    ASSERT_TRUE(presets.isValid());
    MockEngine engine{PresetStore{presets.path()}};
    Listener heard(engine);
    engine.createSession(u"Presets"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"v"_s, u"effect.volume"_s));
    const int built = heard.patches.count();

    // **The engine reads the preset**, so nothing above the facade has to
    // know what a preset contains.
    engine.listPresets(u"effect.volume"_s);
    ASSERT_EQ(heard.presets.count(), 1);
    const QJsonArray offered = QJsonDocument::fromJson(heard.presets.back().at(1).toString().toUtf8()).array();
    ASSERT_FALSE(offered.isEmpty()) << "no presets were offered for a block that ships three";
    const QString first = offered.at(0).toObject().value(u"id"_s).toString();
    EXPECT_TRUE(offered.at(0).toObject().value(u"factory"_s).toBool()) << "the shipped ones come first";
    EXPECT_TRUE(offered.at(0).toObject().value(u"name"_s).toString().isEmpty())
        << "a factory preset's words belong to the interface";

    engine.applyPreset(id, u"v"_s, first);
    ASSERT_TRUE(heard.errors.isEmpty()) << heard.lastErrorReason().toStdString();
    // **One patch**, not one per parameter.
    ASSERT_EQ(heard.patches.count(), built + 1) << "applying a preset was more than one edit";
    const QJsonObject patch = QJsonDocument::fromJson(heard.patches.back().at(2).toString().toUtf8()).object();
    EXPECT_EQ(patch.value(u"op"_s).toString(), u"applyPreset"_s);
    EXPECT_EQ(patch.value(u"preset"_s).toString(), first);

    // And one undo takes the whole of it back.
    engine.undo(id);
    EXPECT_EQ(heard.patches.count(), built + 2);
    engine.resynchronise(id);
    const QJsonDocument after = QJsonDocument::fromJson(heard.snapshots.back().at(2).toString().toUtf8());
    const QJsonObject block = after.object().value(u"blocks"_s).toArray().at(0).toObject();
    EXPECT_TRUE(block.value(u"preset"_s).toString().isEmpty()) << "undo left the block showing a preset";
}

TEST(MockEngineTest, SavingAPresetKeepsEveryParameterAndLeavesTheBlockShowingIt)
{
    QTemporaryDir presets;
    ASSERT_TRUE(presets.isValid());
    MockEngine engine{PresetStore{presets.path()}};
    Listener heard(engine);
    engine.createSession(u"Presets"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"v"_s, u"effect.volume"_s));
    engine.setParam(id, u"v"_s, u"gain"_s, paramJson(0.75));

    engine.savePreset(id, u"v"_s, u"Mine"_s, false);
    ASSERT_TRUE(heard.errors.isEmpty()) << heard.lastErrorReason().toStdString();

    ASSERT_GE(heard.presets.count(), 1);
    const QJsonArray offered = QJsonDocument::fromJson(heard.presets.back().at(1).toString().toUtf8()).array();
    const auto mine = std::ranges::find_if(
        offered, [](const QJsonValue &entry) { return entry.toObject().value(u"name"_s).toString() == u"Mine"_s; });
    ASSERT_NE(mine, offered.end()) << "the preset that was just saved is not offered";
    const QJsonObject params = mine->toObject().value(u"params"_s).toObject();
    EXPECT_DOUBLE_EQ(params.value(u"gain"_s).toDouble(), 0.75);
    // **Every parameter, not only the one that was touched.** A session
    // holds a parameter once it has been set, so a preset built from the
    // session alone would be missing everything left at its default.
    EXPECT_TRUE(params.contains(u"overdrive"_s)) << "a parameter left at its default was not saved";
    EXPECT_TRUE(params.contains(u"muted"_s));

    // The block is left showing what it was just saved as, rather than
    // "Manual".
    engine.resynchronise(id);
    const QJsonDocument after = QJsonDocument::fromJson(heard.snapshots.back().at(2).toString().toUtf8());
    const QJsonObject block = after.object().value(u"blocks"_s).toArray().at(0).toObject();
    EXPECT_EQ(block.value(u"preset"_s).toString(), u"Mine"_s);

    // Saving onto it is refused unless the caller says to replace it.
    engine.savePreset(id, u"v"_s, u"Mine"_s, false);
    ASSERT_FALSE(heard.errors.isEmpty());
    EXPECT_EQ(heard.lastErrorCode(), u"preset.refused"_s);
    EXPECT_EQ(heard.lastErrorReason(), u"alreadyExists"_s);
}

TEST(MockEngineTest, AShippedPresetCannotBeRenamedOrDeletedThroughTheFacade)
{
    QTemporaryDir presets;
    ASSERT_TRUE(presets.isValid());
    MockEngine engine{PresetStore{presets.path()}};
    Listener heard(engine);
    engine.listPresets(u"effect.dynamics"_s);
    ASSERT_EQ(heard.presets.count(), 1);
    const QJsonArray offered = QJsonDocument::fromJson(heard.presets.back().at(1).toString().toUtf8()).array();
    ASSERT_FALSE(offered.isEmpty());
    const QString shipped = offered.at(0).toObject().value(u"id"_s).toString();

    engine.renamePreset(u"effect.dynamics"_s, shipped, u"Mine"_s);
    EXPECT_EQ(heard.lastErrorCode(), u"preset.refused"_s);
    EXPECT_EQ(heard.lastErrorReason(), u"readOnly"_s);

    engine.deletePreset(u"effect.dynamics"_s, shipped);
    EXPECT_EQ(heard.lastErrorReason(), u"readOnly"_s);

    // And it is still offered afterwards.
    engine.listPresets(u"effect.dynamics"_s);
    const QJsonArray again = QJsonDocument::fromJson(heard.presets.back().at(1).toString().toUtf8()).array();
    EXPECT_EQ(again.size(), offered.size());
}

TEST(MockEngineTest, APresetForAnotherKindOfBlockIsRefused)
{
    // Putting a Dynamics preset on a Volume block would set parameters it
    // does not have and leave the ones it does wherever they were.
    QTemporaryDir presets;
    ASSERT_TRUE(presets.isValid());
    MockEngine engine{PresetStore{presets.path()}};
    Listener heard(engine);
    engine.createSession(u"Presets"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"v"_s, u"effect.volume"_s));
    const int built = heard.patches.count();

    engine.listPresets(u"effect.dynamics"_s);
    const QJsonArray dynamics = QJsonDocument::fromJson(heard.presets.back().at(1).toString().toUtf8()).array();
    ASSERT_FALSE(dynamics.isEmpty());

    engine.applyPreset(id, u"v"_s, dynamics.at(0).toObject().value(u"id"_s).toString());
    EXPECT_EQ(heard.lastErrorCode(), u"preset.refused"_s);
    EXPECT_EQ(heard.patches.count(), built) << "a refused preset still changed the block";
}

TEST(MockEngineTest, ReportsFailuresAsCodesAndArgumentsNeverAsText)
{
    MockEngine engine;
    Listener heard(engine);
    engine.openSession(u"nothing-like-a-session"_s);

    ASSERT_EQ(heard.errors.count(), 1);
    EXPECT_EQ(heard.errors.back().at(0).toString(), u"session.notFound"_s);
    const QJsonDocument args = QJsonDocument::fromJson(heard.errors.back().at(1).toString().toUtf8());
    ASSERT_TRUE(args.isObject());
    EXPECT_EQ(args.object().value(u"session"_s).toString(), u"nothing-like-a-session"_s);
}

TEST(MockEngineTest, HasAnInventoryToDrawBeforeAnythingIsRunning)
{
    MockEngine engine;
    Listener heard(engine);

    engine.getInventory();
    ASSERT_EQ(heard.inventory.count(), 1);
    const QJsonObject empty = QJsonDocument::fromJson(heard.inventory.back().at(0).toString().toUtf8()).object();
    EXPECT_TRUE(empty.value(u"outputs"_s).toArray().isEmpty());

    engine.addFakeInventory();
    ASSERT_EQ(heard.inventory.count(), 2);
    const QJsonObject full = QJsonDocument::fromJson(heard.inventory.back().at(0).toString().toUtf8()).object();
    EXPECT_FALSE(full.value(u"applications"_s).toArray().isEmpty());
    // The made-up inventory has the shape the real one has, match rules and
    // all, so an interface drawn against it needs nothing changed later.
    EXPECT_EQ(full.value(u"applications"_s).toArray().at(0).toObject().value(u"key"_s).toString(), u"binary:firefox"_s);
    EXPECT_FALSE(full.value(u"inputs"_s).toArray().isEmpty());
    EXPECT_FALSE(full.value(u"outputs"_s).toArray().isEmpty());
    EXPECT_FALSE(full.value(u"defaults"_s).toObject().value(u"sink"_s).toString().isEmpty());
}

TEST(MockEngineTest, ListsTheSessionsItIsHolding)
{
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"One"_s);
    engine.createSession(u"Two"_s);

    engine.listSessions();
    ASSERT_EQ(heard.listed.count(), 1);
    const QJsonArray sessions = QJsonDocument::fromJson(heard.listed.back().at(0).toString().toUtf8()).array();
    ASSERT_EQ(sessions.size(), 2);
    std::vector<QString> names{sessions.at(0).toObject().value(u"name"_s).toString(),
                               sessions.at(1).toObject().value(u"name"_s).toString()};
    std::ranges::sort(names);
    EXPECT_EQ(names[0], u"One"_s);
    EXPECT_EQ(names[1], u"Two"_s);
}

TEST(EngineFacadeThreading, EveryCallIsASlotSoItCanBeQueuedToTheEngineThread)
{
    // The engine runs on its own thread and the GUI must never wait on it
    // ([ADR-0003]). That works only if every call is a slot: a queued
    // invocation returns at once, a direct call does not. A method added as
    // a plain function would compile and then block the GUI thread, which is
    // exactly the failure this is here to prevent.
    const MockEngine engine;
    const QMetaObject *meta = engine.metaObject();
    const QMetaObject *facade = &EngineFacade::staticMetaObject;

    int slots = 0;
    for (int index = facade->methodOffset(); index < facade->methodCount(); ++index) {
        const QMetaMethod method = facade->method(index);
        if (method.methodType() == QMetaMethod::Slot && method.access() == QMetaMethod::Public) {
            ++slots;
            // And the implementation really answers it, rather than merely
            // declaring something of the same name.
            EXPECT_GE(meta->indexOfSlot(method.methodSignature().constData()), 0)
                << method.methodSignature().toStdString();
        }
    }
    EXPECT_GE(slots, 15) << "the interface has fewer slots than the XML has methods";
}

TEST(MockEngineTest, TakesBackAnEditAndDoesItAgain)
{
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Podcast"_s);
    const QString id = heard.opened.back().at(0).toString();

    engine.getUndoState(id);
    ASSERT_EQ(heard.undoState.count(), 1) << "a client that asks is answered even when there is nothing to undo";
    EXPECT_FALSE(heard.lastUndoState().at(1).toBool());

    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"source.application"_s));
    ASSERT_EQ(heard.patches.count(), 1);
    ASSERT_GE(heard.undoState.count(), 2);
    EXPECT_TRUE(heard.lastUndoState().at(1).toBool());
    EXPECT_EQ(heard.lastUndoState().at(2).toString(), u"edit.addBlock"_s);
    EXPECT_FALSE(heard.lastUndoState().at(3).toBool());

    // An undo arrives as a patch like any other edit, so a client follows it
    // without knowing it was an undo.
    engine.undo(id);
    ASSERT_EQ(heard.patches.count(), 2);
    const QJsonObject patch = QJsonDocument::fromJson(heard.patches.back().at(2).toString().toUtf8()).object();
    EXPECT_EQ(patch.value(u"op"_s).toString(), u"removeBlock"_s);
    EXPECT_GT(heard.patches.back().at(1).toULongLong(), 0U);
    EXPECT_FALSE(heard.lastUndoState().at(1).toBool());
    EXPECT_TRUE(heard.lastUndoState().at(3).toBool());
    EXPECT_EQ(heard.lastUndoState().at(4).toString(), u"edit.addBlock"_s);

    engine.redo(id);
    ASSERT_EQ(heard.patches.count(), 3);
    EXPECT_EQ(
        QJsonDocument::fromJson(heard.patches.back().at(2).toString().toUtf8()).object().value(u"op"_s).toString(),
        u"addBlock"_s);
    EXPECT_TRUE(heard.lastUndoState().at(1).toBool());

    // Nothing left to redo, and asking for it is not an error.
    engine.redo(id);
    EXPECT_EQ(heard.patches.count(), 3);
    EXPECT_TRUE(heard.errors.isEmpty()) << heard.lastErrorCode().toStdString();
}

TEST(MockEngineTest, AnEditThatArrivedAsAListIsOneStepAndAppliesWholeOrNotAtAll)
{
    // **A link is one thing a person drew**, and the session holds it
    // as a wire a channel, because a link only ever joins two ports of the
    // same channel position. Sending the channels separately would leave it
    // to timing to guess that they belong together, and would leave half a
    // link drawn if the second were refused. A list is neither.
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Podcast"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"source.device"_s));
    engine.applyEdit(id, addBlockEdit(u"b2"_s, u"effect.volume"_s));
    // **A gesture of its own to be measured.** A wire drawn straight after a
    // block joins that block's step on purpose, so that adding a block and
    // wiring it up is one Ctrl+Z; moving a tile in between ends that, which
    // is what leaves the link a step to look at by itself.
    engine.applyEdit(id, asJson(editCommandToJson(MoveBlock{u"b2"_s, QPointF{40.0, 10.0}})));
    const int before = heard.patches.count();

    engine.applyEdit(id, connectLinkEdit(u"b1"_s, u"b2"_s, 2));
    ASSERT_TRUE(heard.errors.isEmpty()) << heard.lastErrorCode().toStdString();

    // **One patch a command**, each carrying the revision it made, because a
    // client follows the revisions one at a time however many arrived at
    // once.
    ASSERT_EQ(heard.patches.count(), before + 2);
    EXPECT_TRUE(heard.patches.at(before).at(2).toString().contains(u"connect"_s));
    EXPECT_EQ(heard.patches.back().at(1).toULongLong(), heard.patches.at(before).at(1).toULongLong() + 1);
    EXPECT_EQ(heard.lastUndoState().at(2).toString(), u"edit.connect"_s);

    // And one Ctrl+Z takes the whole link back, not half of it.
    engine.undo(id);
    EXPECT_EQ(heard.patches.count(), before + 4) << "the undo took back fewer commands than the link had";
    engine.resynchronise(id);
    const QJsonObject session = QJsonDocument::fromJson(heard.snapshots.back().at(2).toString().toUtf8()).object();
    EXPECT_TRUE(session.value(u"wires"_s).toArray().isEmpty()) << "half the link was left behind";
    EXPECT_EQ(heard.lastUndoState().at(2).toString(), u"edit.moveBlock"_s)
        << "the link was more than one step to take back";
}

TEST(MockEngineTest, AListWithSomethingRefusedInItChangesNothingAtAll)
{
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Podcast"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"source.device"_s));
    engine.applyEdit(id, addBlockEdit(u"b2"_s, u"effect.volume"_s));
    const int before = heard.patches.count();

    // The second channel names a port neither block has. **Whole or not at
    // all**: the first must not be left drawn on its own.
    engine.applyEdit(id, connectLinkEdit(u"b1"_s, u"b2"_s, 3));

    EXPECT_FALSE(heard.errors.isEmpty()) << "a link that cannot be drawn was taken";
    EXPECT_EQ(heard.patches.count(), before) << "half a link was drawn before the rest was refused";
    engine.resynchronise(id);
    const QJsonObject session = QJsonDocument::fromJson(heard.snapshots.back().at(2).toString().toUtf8()).object();
    EXPECT_TRUE(session.value(u"wires"_s).toArray().isEmpty());
}

TEST(MockEngineTest, ADragOfAKnobComesBackInOne)
{
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Podcast"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"gain.simple"_s));
    const int afterTheEdit = heard.undoState.count();

    for (int frame = 0; frame < 60; ++frame) {
        // **Wrapped, as the interface wraps it.** Sent bare these did not
        // parse at all, and the drag this test is about never happened —
        // the values were quietly stored as null instead.
        engine.setParam(id, u"b1"_s, u"gain"_s, paramJson(static_cast<double>(frame) / 59.0));
    }
    // A knob turning is not a patch, and the same answer about undo is not
    // sent sixty times.
    EXPECT_EQ(heard.patches.count(), 1);
    EXPECT_EQ(heard.undoState.count(), afterTheEdit + 1) << "the drag sent an answer per frame";

    engine.undo(id);
    // One undo, and the whole drag is gone: the knob is back where it was.
    const QJsonObject patch = QJsonDocument::fromJson(heard.patches.back().at(2).toString().toUtf8()).object();
    EXPECT_EQ(patch.value(u"op"_s).toString(), u"setParam"_s);
    EXPECT_EQ(heard.lastUndoState().at(2).toString(), u"edit.addBlock"_s) << "the block itself should be next";
}

TEST(MockEngineTest, ClosingASessionForgetsWhatWasDoneToIt)
{
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Podcast"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"b1"_s, u"gain.simple"_s));
    EXPECT_TRUE(heard.lastUndoState().at(1).toBool());

    engine.closeSession(id);
    const auto said = heard.undoState.count();
    engine.getUndoState(id);
    EXPECT_EQ(heard.undoState.count(), said) << "a session that is not open has nothing to say about undo";
    EXPECT_EQ(heard.lastErrorCode(), u"session.notFound"_s);

    // And the next session starts with nothing to take back.
    engine.createSession(u"Another"_s);
    engine.getUndoState(heard.opened.back().at(0).toString());
    EXPECT_FALSE(heard.lastUndoState().at(1).toBool());
}

TEST(MockEngineTest, AUrlIsAskedForOncePerStartAndNeverOpenedHere)
{
    // **The engine says what should be opened and never opens it.** Opening
    // a URL is the desktop's business — KIO in the interface, xdg-open in
    // the daemon — and the engine has no business starting processes on
    // somebody's session bus.
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Call"_s);
    const QString id = heard.opened.back().at(0).toString();

    engine.applyEdit(id, addBlockEdit(u"src"_s, u"source.application"_s));
    engine.setParam(id, u"src"_s, u"openUrlOnStart"_s, u"[true]"_s);
    engine.setParam(id, u"src"_s, u"url"_s, u"[\"https://example.invalid/call\"]"_s);
    ASSERT_TRUE(heard.errors.isEmpty()) << heard.lastErrorCode().toStdString();

    engine.start(id);
    ASSERT_EQ(heard.openUrl.count(), 1);
    EXPECT_EQ(heard.openUrl.back().at(0).toString(), id);
    EXPECT_EQ(heard.openUrl.back().at(1).toString(), u"https://example.invalid/call"_s);

    // **Once per start, not once per Start.** A second call on a session
    // that is already running is not a start, and must not open the call
    // again.
    engine.start(id);
    EXPECT_EQ(heard.openUrl.count(), 1);

    // Stopped and started again is a start, and is asked for again.
    engine.stop(id);
    engine.start(id);
    EXPECT_EQ(heard.openUrl.count(), 2);

    // A session with nothing asking for a URL says nothing.
    engine.createSession(u"Quiet"_s);
    const QString quiet = heard.opened.back().at(0).toString();
    engine.applyEdit(quiet, addBlockEdit(u"src"_s, u"source.application"_s));
    engine.start(quiet);
    EXPECT_EQ(heard.openUrl.count(), 2);
}

TEST(MockEngineTest, SaysWhatItIsRunningOnAndPretendsItCanGoAway)
{
    // **The one thing an interface cannot rehearse on a working desktop.**
    // What a window does when the audio system is not running has to be
    // built and tested somehow, and stopping the real server under a test
    // suite is not somehow.
    MockEngine engine;
    QSignalSpy system(&engine, &EngineFacade::audioSystemChanged);
    Listener heard(engine);

    engine.getAudioSystem();
    ASSERT_EQ(system.count(), 1);
    const AudioSystemInfo running = audioSystemFromJson(system.back().at(0).toString());
    EXPECT_TRUE(running.isConnected());
    EXPECT_FALSE(running.serverName.isEmpty());
    EXPECT_FALSE(running.serverVersion.isEmpty());
    EXPECT_FALSE(running.sessionManagerName.isEmpty());

    engine.createSession(u"Podcast"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.start(id);
    ASSERT_EQ(heard.states.back().at(1).toString(), QString(sessionState::running));

    // The server goes: the session stops, and the interface is told why
    // without having to ask.
    engine.setAudioSystemRunning(false);
    EXPECT_EQ(heard.states.back().at(1).toString(), QString(sessionState::idle));
    ASSERT_EQ(system.count(), 2);
    EXPECT_FALSE(audioSystemFromJson(system.back().at(0).toString()).isConnected());

    // Running it now is refused the way the engine refuses it: a code and
    // an argument, never text.
    engine.start(id);
    EXPECT_EQ(heard.lastErrorCode(), u"session.cannotRun"_s);
    EXPECT_EQ(heard.states.back().at(1).toString(), QString(sessionState::error));

    // **A server that is back is not a connection to it.** Something has to
    // ask, which is what the Retry in the interface does.
    engine.setAudioSystemRunning(true);
    EXPECT_EQ(system.count(), 2) << "the mock claimed a connection nobody asked for";
    engine.reconnectAudioSystem();
    ASSERT_EQ(system.count(), 3);
    EXPECT_TRUE(audioSystemFromJson(system.back().at(0).toString()).isConnected());

    engine.start(id);
    EXPECT_EQ(heard.states.back().at(1).toString(), QString(sessionState::running));
}

TEST(MockEngineTest, ARunningDeviceBlockSaysWhetherItsDeviceIsThere)
{
    // **The mock publishes what the real engine publishes**: a
    // device block's first value is 1 while its device is there and 0 while
    // it is not. Without it every running Input and Output Device tile read
    // "Unavailable" against the mock, which is not what anybody sees.
    int argc = 1;
    char name[5] = "mock";
    std::array<char *, 2> argv{static_cast<char *>(name), nullptr};
    // The telemetry is a timer, and a timer needs an event dispatcher.
    const QCoreApplication application(argc, argv.data());

    MockEngine engine;
    Listener heard(engine);
    engine.addFakeInventory();
    engine.createSession(u"Devices"_s);
    const QString id = heard.opened.back().at(0).toString();
    // One following the default, one naming a device, a microphone
    // following the default, and one naming a device the machine does not
    // have.
    engine.applyEdit(id, addBlockEdit(u"out"_s, u"output.device"_s));
    engine.applyEdit(id, addBlockEdit(u"hdmi"_s, u"output.device"_s));
    engine.setParam(id, u"hdmi"_s, u"device"_s, paramJson(u"alsa_output.hdmi"_s));
    engine.applyEdit(id, addBlockEdit(u"mic"_s, u"source.device"_s));
    engine.applyEdit(id, addBlockEdit(u"gone"_s, u"source.device"_s));
    engine.setParam(id, u"gone"_s, u"device"_s, paramJson(u"alsa_input.nothing-like-it"_s));
    ASSERT_TRUE(heard.errors.isEmpty()) << heard.lastErrorCode().toStdString();

    const TelemetrySlot *slot = engine.telemetryOf(id);
    ASSERT_NE(slot, nullptr);
    engine.start(id);
    ASSERT_EQ(heard.states.back().at(1).toString(), QString(sessionState::running));

    // What the last snapshot says about whether \a blockId's device is there.
    const auto present = [slot](std::string_view blockId) -> std::optional<float> {
        const TelemetrySnapshot snapshot = slot->load();
        for (std::uint32_t block = 0; block < snapshot.blockCount; ++block) {
            if (std::string_view(static_cast<const char *>(snapshot.blocks[block].id)) == blockId) {
                return snapshot.blocks[block].values[rb::blocks::OutputDevice::kDevicePresentValue];
            }
        }
        return std::nullopt;
    };
    // Turns the event loop until a snapshot newer than \a after is out.
    const auto nextFrame = [slot](std::uint64_t after) {
        QElapsedTimer waited;
        waited.start();
        while (slot->load().frame <= after && waited.elapsed() < 4000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        }
    };
    nextFrame(slot->load().frame);
    EXPECT_EQ(present("out"), 1.0f);
    EXPECT_EQ(present("hdmi"), 1.0f);
    EXPECT_EQ(present("mic"), 1.0f);
    EXPECT_EQ(present("gone"), 0.0f);

    // Unplugged: the block says so while it runs, and says so again when
    // the device comes back.
    engine.setFakeDevicePresent(u"alsa_output.hdmi"_s, false);
    nextFrame(slot->load().frame);
    EXPECT_EQ(present("hdmi"), 0.0f);
    EXPECT_EQ(present("out"), 1.0f) << "the default is another device, and it is still there";

    engine.setFakeDevicePresent(u"alsa_output.hdmi"_s, true);
    nextFrame(slot->load().frame);
    EXPECT_EQ(present("hdmi"), 1.0f);

    engine.stop(id);
}

// Pausing, resuming and splitting a recording: the mock answers the
// way the engine does, refusals in the engine's order and words included.
TEST(MockEngineTest, ARecordingIsPausedResumedAndSplitOrRefusedAsTheEngineWould)
{
    MockEngine engine;
    Listener heard(engine);
    QSignalSpy paused(&engine, &EngineFacade::recordingPaused);
    engine.createSession(u"Take"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"rec"_s, u"output.recorder"_s));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, u"effect.volume"_s));

    const auto refusedFor = [&heard](const char *reason) {
        return heard.lastErrorCode() == "recording.refused"_L1
            && heard.errors.back().at(1).toString().contains(QLatin1StringView{reason});
    };
    engine.controlRecording(id, u"rec"_s, u"pause"_s);
    EXPECT_TRUE(refusedFor("NotRunning"));

    engine.start(id);
    engine.controlRecording(id, u"nothing"_s, u"pause"_s);
    EXPECT_TRUE(refusedFor("blockNotFound"));
    engine.controlRecording(id, u"fx"_s, u"pause"_s);
    EXPECT_TRUE(refusedFor("notARecorder"));
    engine.controlRecording(id, u"rec"_s, u"rewind"_s);
    EXPECT_TRUE(refusedFor("unknownAction"));

    const qsizetype errorsBefore = heard.errors.count();
    engine.controlRecording(id, u"rec"_s, u"pause"_s);
    engine.controlRecording(id, u"rec"_s, u"resume"_s);
    engine.controlRecording(id, u"rec"_s, u"split"_s);
    EXPECT_EQ(heard.errors.count(), errorsBefore);
    ASSERT_EQ(paused.count(), 2);
    EXPECT_TRUE(paused.at(0).at(2).toBool());
    EXPECT_FALSE(paused.at(1).at(2).toBool());
}

// Restarting a plugin: the mock refuses as the engine does, and
// takes a plugin block that is on as something it may restart.
TEST(MockEngineTest, APluginIsRestartedOrRefusedAsTheEngineWould)
{
    MockEngine engine;
    Listener heard(engine);
    engine.createSession(u"Plugins"_s);
    const QString id = heard.opened.back().at(0).toString();
    engine.applyEdit(id, addBlockEdit(u"p"_s, u"plugin.builtin"_s));
    engine.applyEdit(id, addBlockEdit(u"fx"_s, u"effect.volume"_s));

    const auto refusedFor = [&heard](const char *reason) {
        return heard.lastErrorCode() == "plugin.restartRefused"_L1
            && heard.errors.back().at(1).toString().contains(QLatin1StringView{reason});
    };
    engine.restartPlugin(id, u"p"_s);
    EXPECT_TRUE(refusedFor("NotRunning"));
    engine.start(id);
    engine.restartPlugin(id, u"nothing"_s);
    EXPECT_TRUE(refusedFor("blockNotFound"));
    engine.restartPlugin(id, u"fx"_s);
    EXPECT_TRUE(refusedFor("notAHostedPlugin"));

    const qsizetype errorsBefore = heard.errors.count();
    engine.restartPlugin(id, u"p"_s);
    EXPECT_EQ(heard.errors.count(), errorsBefore);
}

// The plugins installed on this machine: the mock lists the host's
// built-in ones and whatever a test adds, and a rescan is said to start and
// to end, as the engine says it.
TEST(MockEngineTest, ListsPluginsAndSaysARescanStartsAndEnds)
{
    // The end of a rescan is queued, as the engine's is: an event loop to
    // run it needs an application.
    int argc = 1;
    char name[5] = "mock";
    std::array<char *, 2> argv{static_cast<char *>(name), nullptr};
    const QCoreApplication application(argc, argv.data());
    MockEngine engine;
    QSignalSpy changed(&engine, &EngineFacade::pluginsChanged);
    engine.listPlugins();
    ASSERT_EQ(changed.count(), 1);
    QJsonObject json = QJsonDocument::fromJson(changed.back().at(0).toString().toUtf8()).object();
    EXPECT_FALSE(json.value("scanning"_L1).toBool());
    const QJsonArray builtIn = json.value("plugins"_L1).toArray();
    ASSERT_EQ(builtIn.size(), 2);
    EXPECT_EQ(builtIn[0].toObject().value("id"_L1).toString(), u"test.gain"_s);
    EXPECT_EQ(builtIn[0].toObject().value("format"_L1).toString(), u"builtin"_s);

    engine.addFakePlugin(u"lv2"_s, u"http://example.org/eq"_s, u"An EQ"_s, u"Somebody"_s);
    engine.addFakeBlockedPlugin(u"lv2"_s, u"/somewhere/broken.lv2"_s, u"crashed"_s);
    json = QJsonDocument::fromJson(changed.back().at(0).toString().toUtf8()).object();
    EXPECT_EQ(json.value("plugins"_L1).toArray().size(), 3);
    EXPECT_EQ(json.value("blocked"_L1).toArray().at(0).toObject().value("reason"_L1).toString(), u"crashed"_s);

    changed.clear();
    engine.rescanPlugins(true);
    ASSERT_EQ(changed.count(), 1);
    EXPECT_TRUE(
        QJsonDocument::fromJson(changed.back().at(0).toString().toUtf8()).object().value("scanning"_L1).toBool());
    ASSERT_TRUE(changed.wait(1000));
    EXPECT_FALSE(
        QJsonDocument::fromJson(changed.back().at(0).toString().toUtf8()).object().value("scanning"_L1).toBool());
    EXPECT_EQ(engine.fakeRescans(), 1);
    EXPECT_TRUE(engine.fakeLastRescanRetried());
}

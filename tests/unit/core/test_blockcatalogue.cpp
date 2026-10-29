// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What blocks this build has, and what each of them can be asked. The
// settings panel is generated from exactly this, so a block that describes
// itself badly here is a block with no interface.

#include "blocks/applicationsource.h"
#include "blocks/builtinblocks.h"
#include "blocks/ducking.h"
#include "blocks/dynamics.h"
#include "blocks/inputdevice.h"
#include "blocks/mixer.h"
#include "blocks/outputdevice.h"
#include "blocks/systemaudio.h"
#include "blocks/volume.h"
#include "core/blockcatalogue.h"

#include <QJsonArray>
#include <QJsonObject>
#include <gtest/gtest.h>

#include <expected>
#include <string_view>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] dsp::BlockRegistry builtins()
{
    dsp::BlockRegistry registry;
    EXPECT_TRUE(blocks::registerBuiltinBlocks(registry));
    return registry;
}

/// One block's entry, by type id.
[[nodiscard]] QJsonObject blockOf(const QJsonObject &catalogue, const QString &typeId)
{
    for (const QJsonValue &entry : catalogue.value("blocks"_L1).toArray()) {
        if (entry.toObject().value("type"_L1).toString() == typeId) {
            return entry.toObject();
        }
    }
    return {};
}

/// One parameter of a block, by id.
[[nodiscard]] QJsonObject paramOf(const QJsonObject &block, const QString &id)
{
    for (const QJsonValue &entry : block.value("params"_L1).toArray()) {
        if (entry.toObject().value("id"_L1).toString() == id) {
            return entry.toObject();
        }
    }
    return {};
}

[[nodiscard]] QString typeIdOf(std::string_view id)
{
    return QString::fromUtf8(id.data(), static_cast<qsizetype>(id.size()));
}

} // namespace

TEST(BlockCatalogueTest, EveryBlockTheBuildHasIsInIt)
{
    const dsp::BlockRegistry registry = builtins();
    const QJsonObject catalogue = blockCatalogueJson(registry);
    const QJsonArray blocks = catalogue.value("blocks"_L1).toArray();

    EXPECT_EQ(static_cast<std::size_t>(blocks.size()), registry.size());
    for (const std::string &typeId : registry.typeIds()) {
        const QJsonObject block = blockOf(catalogue, QString::fromStdString(typeId));
        EXPECT_FALSE(block.isEmpty()) << typeId << " is not in the catalogue";
        // A tile cannot be drawn without these: what to file it under, what
        // its face shows, and how many ports it has.
        EXPECT_FALSE(block.value("category"_L1).toString().isEmpty()) << typeId;
        EXPECT_FALSE(block.value("face"_L1).toString().isEmpty()) << typeId;
        EXPECT_TRUE(block.contains("inputs"_L1)) << typeId;
        EXPECT_TRUE(block.contains("outputs"_L1)) << typeId;
        EXPECT_TRUE(block.contains("params"_L1)) << typeId;
    }
}

TEST(BlockCatalogueTest, EveryParameterSaysEnoughToDrawAControlFor)
{
    // The criterion behind the criterion: the generic panel chooses a
    // slider, a spin box, a combo or a switch from the type, the unit and
    // the taper, and it cannot choose without all three.
    const QJsonObject catalogue = blockCatalogueJson(builtins());
    for (const QJsonValue &entry : catalogue.value("blocks"_L1).toArray()) {
        const QJsonObject block = entry.toObject();
        const QString typeId = block.value("type"_L1).toString();
        for (const QJsonValue &parameter : block.value("params"_L1).toArray()) {
            const QJsonObject param = parameter.toObject();
            const QString id = param.value("id"_L1).toString();
            EXPECT_FALSE(id.isEmpty()) << typeId.toStdString() << " has a parameter with no id";
            EXPECT_FALSE(param.value("type"_L1).toString().isEmpty())
                << typeId.toStdString() << '.' << id.toStdString();
            EXPECT_TRUE(param.contains("unit"_L1)) << typeId.toStdString() << '.' << id.toStdString();
            EXPECT_TRUE(param.contains("taper"_L1)) << typeId.toStdString() << '.' << id.toStdString();
            // **A default is what a control opens at**, and every parameter
            // has one: a panel with nothing to show is a panel that is
            // wrong before anybody touches it.
            EXPECT_TRUE(param.contains("default"_L1)) << typeId.toStdString() << '.' << id.toStdString();

            // A ranged type has both ends; anything else has neither.
            const QString type = param.value("type"_L1).toString();
            const bool ranged = type == u"float"_s || type == u"int"_s;
            EXPECT_EQ(param.contains("minimum"_L1), ranged) << typeId.toStdString() << '.' << id.toStdString();
            EXPECT_EQ(param.contains("maximum"_L1), ranged) << typeId.toStdString() << '.' << id.toStdString();
            EXPECT_EQ(param.contains("choices"_L1), type == u"enum"_s)
                << typeId.toStdString() << '.' << id.toStdString();
        }
    }
}

TEST(BlockCatalogueTest, ARangeAnEnumAndASwitchEachLookLikeThemselves)
{
    const QJsonObject catalogue = blockCatalogueJson(builtins());
    const QJsonObject volume = blockOf(catalogue, typeIdOf(blocks::Volume::kTypeId));
    ASSERT_FALSE(volume.isEmpty());

    const QJsonObject gain = paramOf(volume, u"gain"_s);
    ASSERT_FALSE(gain.isEmpty());
    EXPECT_EQ(gain.value("type"_L1).toString(), u"float"_s);
    EXPECT_DOUBLE_EQ(gain.value("minimum"_L1).toDouble(), 0.0);
    EXPECT_DOUBLE_EQ(gain.value("maximum"_L1).toDouble(), 4.0);
    EXPECT_DOUBLE_EQ(gain.value("default"_L1).toDouble(), 1.0);
    // The smoothing is there so the panel knows a change will glide rather
    // than step, which is what makes a drag safe to send at 60 Hz.
    EXPECT_DOUBLE_EQ(gain.value("smoothingMs"_L1).toDouble(), static_cast<double>(blocks::Volume::kGainSmoothingMs));

    const QJsonObject overdrive = paramOf(volume, u"overdrive"_s);
    ASSERT_FALSE(overdrive.isEmpty());
    EXPECT_EQ(overdrive.value("type"_L1).toString(), u"enum"_s);
    EXPECT_EQ(overdrive.value("choices"_L1).toArray().size(), 4);
    EXPECT_EQ(overdrive.value("choices"_L1).toArray().at(0).toString(), u"off"_s);
    EXPECT_EQ(overdrive.value("default"_L1).toString(), u"off"_s);

    const QJsonObject muted = paramOf(volume, u"muted"_s);
    ASSERT_FALSE(muted.isEmpty());
    EXPECT_EQ(muted.value("type"_L1).toString(), u"bool"_s);
    EXPECT_TRUE(muted.value("default"_L1).isBool());
    EXPECT_FALSE(muted.value("default"_L1).toBool());
    // A switch has no range and no smoothing of its own.
    EXPECT_FALSE(muted.contains("minimum"_L1));
    EXPECT_FALSE(muted.contains("smoothingMs"_L1));
}

TEST(BlockCatalogueTest, ATaperIsCarriedBecauseARatioIsNotLinear)
{
    // Equal distances along a ratio slider should be equal multiples, and
    // the panel cannot know that without being told.
    const QJsonObject catalogue = blockCatalogueJson(builtins());
    const QJsonObject dynamics = blockOf(catalogue, typeIdOf(blocks::Dynamics::kTypeId));
    ASSERT_FALSE(dynamics.isEmpty());

    // The names are the ones paramTaperName() and paramUnitName() already
    // give: short, stable, and the same strings the rest of the facade uses.
    EXPECT_EQ(paramOf(dynamics, u"ratio"_s).value("taper"_L1).toString(), u"log"_s);
    EXPECT_EQ(paramOf(dynamics, u"threshold"_s).value("taper"_L1).toString(), u"linear"_s);
    EXPECT_EQ(paramOf(dynamics, u"threshold"_s).value("unit"_L1).toString(), u"dB"_s);
    EXPECT_EQ(paramOf(dynamics, u"attack"_s).value("unit"_L1).toString(), u"ms"_s);
    EXPECT_EQ(paramOf(dynamics, u"ratio"_s).value("unit"_L1).toString(), u"ratio"_s);
}

TEST(BlockCatalogueTest, AParameterTheSessionPlanReadsSaysSoInTheCatalogue)
{
    // Which parameters are structural is the block's to declare, and
    // the catalogue is how it reaches everything else. Changing one of
    // these has to move links and ports, which is why the engine plans the
    // session again for it instead of sending the audio thread a number.
    const QJsonObject catalogue = blockCatalogueJson(builtins());
    const auto automatable = [&catalogue](std::string_view type, const QString &param) {
        const QJsonObject block = blockOf(catalogue, typeIdOf(type));
        EXPECT_FALSE(block.isEmpty()) << type;
        const QJsonObject entry = paramOf(block, param);
        EXPECT_FALSE(entry.isEmpty()) << param.toStdString();
        // Absent means automatable: only the parameters that are not say so.
        return entry.value("automatable"_L1).toBool(true);
    };

    EXPECT_FALSE(automatable(blocks::OutputDevice::kTypeId, u"device"_s))
        << "choosing another output would be ignored until the session was restarted";
    EXPECT_FALSE(automatable(blocks::InputDevice::kTypeId, u"device"_s));
    EXPECT_FALSE(automatable(blocks::SystemAudio::kTypeId, u"includeAlerts"_s));
    EXPECT_FALSE(automatable(blocks::SystemAudio::kTypeId, u"excluded"_s));
    for (const QString &param : {u"binary"_s,
                                 u"flatpakId"_s,
                                 u"name"_s,
                                 u"outputMode"_s,
                                 u"outputs"_s,
                                 u"includeInput"_s,
                                 u"openUrlOnStart"_s,
                                 u"url"_s}) {
        EXPECT_FALSE(automatable(blocks::ApplicationSource::kTypeId, param)) << param.toStdString();
    }

    // **And the knobs are still knobs.** A parameter marked structural by
    // mistake would cost a recompile for every value of a drag.
    EXPECT_TRUE(automatable(blocks::Volume::kTypeId, u"gain"_s));
    EXPECT_TRUE(automatable(blocks::Volume::kTypeId, u"muted"_s));
    EXPECT_TRUE(automatable(blocks::OutputDevice::kTypeId, u"volume"_s));
    EXPECT_TRUE(automatable(blocks::Dynamics::kTypeId, u"threshold"_s));
}

TEST(BlockCatalogueTest, NothingStructuralIsSomethingAControlCouldDrag)
{
    // **The one case the engine could not coalesce.** A value that streams
    // while a control moves comes from a slider, and the generic panel
    // gives a slider to a float and to nothing else. So long as that holds,
    // a structural change arrives once, when the choice is made, and one
    // replan for each is one replan per gesture. A float a block wants to
    // be structural needs this test changed and the live path answered
    // along with it.
    const QJsonObject catalogue = blockCatalogueJson(builtins());
    for (const QJsonValue &entry : catalogue.value("blocks"_L1).toArray()) {
        const QJsonObject block = entry.toObject();
        for (const QJsonValue &parameter : block.value("params"_L1).toArray()) {
            const QJsonObject param = parameter.toObject();
            if (param.value("automatable"_L1).toBool(true)) {
                continue;
            }
            EXPECT_NE(param.value("type"_L1).toString(), u"float"_s)
                << block.value("type"_L1).toString().toStdString() << '.'
                << param.value("id"_L1).toString().toStdString() << " is structural and would be given a slider";
        }
    }
}

TEST(BlockCatalogueTest, ItHoldsIdsAndNeverWords)
{
    // The engine has no idea what language anybody speaks, so there is
    // nothing here for a person to read. If a name or a description ever
    // appears, the translations have been bypassed.
    const QJsonObject catalogue = blockCatalogueJson(builtins());
    for (const QJsonValue &entry : catalogue.value("blocks"_L1).toArray()) {
        const QJsonObject block = entry.toObject();
        EXPECT_FALSE(block.contains("name"_L1)) << "the catalogue carries a name";
        EXPECT_FALSE(block.contains("description"_L1)) << "the catalogue carries a description";
        for (const QJsonValue &parameter : block.value("params"_L1).toArray()) {
            EXPECT_FALSE(parameter.toObject().contains("name"_L1)) << "a parameter carries a name";
            EXPECT_FALSE(parameter.toObject().contains("label"_L1)) << "a parameter carries a label";
        }
    }
}

TEST(BlockCatalogueTest, AWireToAPortNoBlockOfThatTypeHasIsRefused)
{
    // **The one check applyEdit() cannot make.** A session says which type
    // a block is, not how many ports that type has, so the engine makes
    // this one where the registry is — otherwise a wire to a port that
    // does not exist is taken, and the whole session stops compiling the
    // moment somebody presses Start.
    const dsp::BlockRegistry registry = builtins();
    Session session;

    BlockInstance volume;
    volume.id = u"b1"_s;
    volume.type = u"effect.volume"_s; // two in, two out
    session.blocks.append(volume);

    BlockInstance meter;
    meter.id = u"b2"_s;
    meter.type = u"meter.peakrms"_s; // two in, two out
    session.blocks.append(meter);

    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 1, u"b2"_s, 1}), QString());
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 2, u"b2"_s, 0}), u"wirePortOutOfRange"_s);
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 0, u"b2"_s, 2}), u"wirePortOutOfRange"_s);
}

TEST(BlockCatalogueTest, ABlockThisBuildHasNoTypeForHasItsPortsLeftAlone)
{
    // A session written by a later version is kept exactly as the file held
    // it, so guessing at the shape of a block nobody here has ever heard of
    // would be inventing one.
    const dsp::BlockRegistry registry = builtins();
    Session session;

    BlockInstance stranger;
    stranger.id = u"b1"_s;
    stranger.type = u"effect.notInThisBuild"_s;
    stranger.unknownType = true;
    session.blocks.append(stranger);

    BlockInstance volume;
    volume.id = u"b2"_s;
    volume.type = u"effect.volume"_s;
    session.blocks.append(volume);

    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 17, u"b2"_s, 0}), QString());
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 0, u"b2"_s, 17}), u"wirePortOutOfRange"_s)
        << "the end this build does know about is still checked";
}

TEST(BlockCatalogueTest, ASideWithTwoLanesSaysWhereEachStartsAndWhatItIsCalled)
{
    // The canvas draws a connector a lane, so it has to be told how
    // many ports make one and what each is for. A side with one lane says
    // nought, which is every block but the ducker in this build.
    const QJsonObject catalogue = blockCatalogueJson(builtins());
    const QJsonObject ducking = blockOf(catalogue, typeIdOf(blocks::Ducking::kTypeId));
    ASSERT_FALSE(ducking.isEmpty());
    const QJsonObject inputs = ducking.value("inputs"_L1).toObject();
    EXPECT_EQ(inputs.value("laneWidth"_L1).toInt(), 2);
    EXPECT_EQ(inputs.value("lanes"_L1).toArray(), (QJsonArray{u"main"_s, u"background"_s}));
    EXPECT_EQ(inputs.value("minimum"_L1).toInt(), 4);
    const QJsonObject outputs = ducking.value("outputs"_L1).toObject();
    EXPECT_EQ(outputs.value("laneWidth"_L1).toInt(), 0);
    EXPECT_TRUE(outputs.value("lanes"_L1).toArray().isEmpty());

    const QJsonObject volume = blockOf(catalogue, u"effect.volume"_s);
    EXPECT_EQ(volume.value("inputs"_L1).toObject().value("laneWidth"_L1).toInt(), 0);
}

TEST(BlockCatalogueTest, AMixerTakesFiveLanesAndNotASixth)
{
    // The mixer's port limit, where the engine checks it. Five stereo lanes
    // are ports 0 to 9, and a wire to port 10 is refused by name before it
    // ever reaches the compiler, which would refuse the whole session.
    const dsp::BlockRegistry registry = builtins();
    Session session;
    BlockInstance source;
    source.id = u"b1"_s;
    source.type = u"effect.volume"_s;
    session.blocks.append(source);
    BlockInstance mixer;
    mixer.id = u"b2"_s;
    mixer.type = typeIdOf(blocks::Mixer::kTypeId);
    session.blocks.append(mixer);

    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 0, u"b2"_s, 8}), QString{});
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 1, u"b2"_s, 9}), QString{});
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 0, u"b2"_s, 10}), u"wirePortOutOfRange"_s);

    const QJsonObject inputs =
        blockOf(blockCatalogueJson(registry), typeIdOf(blocks::Mixer::kTypeId)).value("inputs"_L1).toObject();
    EXPECT_EQ(inputs.value("maximum"_L1).toInt(), 10);
    EXPECT_EQ(inputs.value("laneWidth"_L1).toInt(), 2);
    EXPECT_EQ(inputs.value("lanes"_L1).toArray().size(), 5);
}

TEST(BlockCatalogueTest, ThePortsTheAudioSystemFillsAreTheFlexibleOnes)
{
    // **Every flexible port in this build is the audio system's**, and that
    // is not a coincidence: a port count that is a range rather than a
    // number is one that follows a device's channel layout, which is the
    // audio system telling the block what shape it is. The block writes the
    // count down itself once the layout is known, so no wire ever has to
    // make one grow — and none can, because these are exactly
    // the ports a wire is refused.
    const dsp::BlockRegistry registry = builtins();
    for (const std::string &typeId : registry.typeIds()) {
        const std::expected<dsp::BlockInfo, dsp::BlockError> info = registry.describe(typeId);
        ASSERT_TRUE(info.has_value()) << typeId;
        if (info->inputs.maxCount > info->inputs.minCount) {
            EXPECT_TRUE(info->inputsFromAudioSystem) << typeId << " has a range of inputs nothing fills";
        }
        if (info->outputs.maxCount > info->outputs.minCount) {
            EXPECT_TRUE(info->outputsToAudioSystem) << typeId << " has a range of outputs nothing takes";
        }
    }
}

TEST(BlockCatalogueTest, AWireIntoTheAudioSystemsPortsIsRefusedByName)
{
    // A capture block's inputs are filled by the application or the
    // device it follows, and an output block's outputs belong to the
    // device. A wire to either is two things writing one signal, and the
    // reason has the name the interface already spells out.
    const dsp::BlockRegistry registry = builtins();
    Session session;

    BlockInstance source;
    source.id = u"b1"_s;
    source.type = u"source.device"_s;
    session.blocks.append(source);

    BlockInstance volume;
    volume.id = u"b2"_s;
    volume.type = u"effect.volume"_s;
    session.blocks.append(volume);

    BlockInstance output;
    output.id = u"b3"_s;
    output.type = u"output.device"_s;
    session.blocks.append(output);

    // The way round a session is built is taken as it always was.
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b1"_s, 0, u"b2"_s, 0}), QString());
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b2"_s, 0, u"b3"_s, 0}), QString());

    // And the two that are not wiring at all, **whatever the port number**:
    // an output device's outputs are a range wide enough to take the wire,
    // and it is still refused.
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b2"_s, 0, u"b1"_s, 0}), u"wireIntoSource"_s);
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b3"_s, 0, u"b2"_s, 0}), u"wireOutOfOutput"_s);

    const std::expected<dsp::BlockInfo, dsp::BlockError> info = registry.describe("output.device");
    ASSERT_TRUE(info.has_value());
    ASSERT_GT(info->outputs.maxCount, info->outputs.minCount) << "this block was meant to be the flexible one";
    EXPECT_EQ(wireRefusal(session, registry, Wire{u"b3"_s, info->outputs.maxCount - 1, u"b2"_s, 0}),
              u"wireOutOfOutput"_s);
}

TEST(BlockCatalogueTest, AnEmptyRegistryIsAnEmptyCatalogueAndNotAnError)
{
    const dsp::BlockRegistry nothing;
    const QJsonObject catalogue = blockCatalogueJson(nothing);
    EXPECT_TRUE(catalogue.contains("blocks"_L1));
    EXPECT_EQ(catalogue.value("blocks"_L1).toArray().size(), 0);
}

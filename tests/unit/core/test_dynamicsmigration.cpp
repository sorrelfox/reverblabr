// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Dynamics Processor's curve became four points: that a session or a
// preset saved before that opens as the nearest such curve, measured against
// what the old curve did, and that the shipped presets still do what they
// are called.

#include "blocks/dynamics.h"
#include "core/dynamicsmigration.h"
#include "core/presetstore.h"
#include "core/sessionstore.h"
#include "dsp/dynamics.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUuid>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

/// A block with \a params, as the session or the preset would hold them.
[[nodiscard]] dsp::DynamicsShape shapeOf(const QJsonObject &params)
{
    dsp::DynamicsShape shape;
    shape.thresholdDb = static_cast<float>(params.value("threshold"_L1).toDouble());
    shape.heightDb = static_cast<float>(params.value("height"_L1).toDouble());
    shape.expanderThresholdDb = static_cast<float>(params.value("expanderThreshold"_L1).toDouble());
    shape.expanderRatio = static_cast<float>(params.value("expanderRatio"_L1).toDouble());
    shape.makeupDb = static_cast<float>(params.value("makeup"_L1).toDouble());
    return shape;
}

/// The old curve at \a inputDb, by the law that is still the compressors'.
[[nodiscard]] dsp::DynamicsSettings oldSettings(float thresholdDb, float ratio, float kneeDb)
{
    dsp::DynamicsSettings settings;
    settings.thresholdDb = thresholdDb;
    settings.ratio = ratio;
    settings.kneeDb = kneeDb;
    return settings;
}

} // namespace

TEST(DynamicsMigration, TheNearestShapeStaysNearTheOldCurveWhereSoundIs)
{
    struct Old {
        float thresholdDb;
        float ratio;
        float kneeDb;
        double allowedDb;
    };
    // **What "nearest" is worth, measured.** The old curves were straight
    // above the knee and the new one is always bent, so a gentle ratio from
    // far down is the worst it gets.
    for (const Old &old : {Old{-20.0f, 20.0f, 12.0f, 1.0},
                           Old{-18.0f, 2.0f, 12.0f, 2.5},
                           Old{-16.0f, 3.0f, 6.0f, 2.0},
                           Old{-24.0f, 4.0f, 6.0f, 2.5},
                           Old{-3.0f, 20.0f, 0.0f, 1.0},
                           Old{-40.0f, 8.0f, 0.0f, 3.5}}) {
        const QJsonObject migrated = dynamicsParamsFromOld(
            QJsonObject{{"threshold"_L1, old.thresholdDb}, {"ratio"_L1, old.ratio}, {"knee"_L1, old.kneeDb}});
        const dsp::DynamicsShape shape = shapeOf(migrated);
        const dsp::DynamicsSettings settings = oldSettings(old.thresholdDb, old.ratio, old.kneeDb);
        double worst = 0.0;
        for (float inputDb = -60.0f; inputDb <= 0.0f; inputDb += 1.0f) {
            worst = std::max(
                worst,
                std::fabs(static_cast<double>(dsp::outputDb(inputDb, shape) - dsp::outputDb(inputDb, settings))));
        }
        EXPECT_LT(worst, old.allowedDb) << "threshold " << old.thresholdDb << " at " << old.ratio << ":1, knee "
                                        << old.kneeDb;
        // A peak at full scale comes out where it came out.
        EXPECT_NEAR(dsp::outputDb(0.0f, shape), dsp::outputDb(0.0f, settings), 0.05f);
    }
}

TEST(DynamicsMigration, ABlockThatMentionedNothingBecomesTheNewDefault)
{
    // Nothing mentioned meant the old defaults, and the new defaults are the
    // nearest curve to those.
    const QJsonObject migrated = dynamicsParamsFromOld(QJsonObject{});
    const blocks::Dynamics block;
    EXPECT_NEAR(migrated.value("threshold"_L1).toDouble(), block.settings().thresholdDb, 0.01);
    EXPECT_NEAR(migrated.value("height"_L1).toDouble(), block.settings().heightDb, 0.25);
    EXPECT_DOUBLE_EQ(migrated.value("expanderThreshold"_L1).toDouble(), block.settings().expanderThresholdDb);
    EXPECT_DOUBLE_EQ(migrated.value("expanderRatio"_L1).toDouble(), 1.0);
}

TEST(DynamicsMigration, ALimiterBecomesAHeightOfNothing)
{
    const dsp::DynamicsShape shape = shapeOf(dynamicsParamsFromOld(
        QJsonObject{{"threshold"_L1, -3.0}, {"ratio"_L1, 20.0}, {"infiniteRatio"_L1, true}, {"knee"_L1, 0.0}}));
    EXPECT_FLOAT_EQ(shape.thresholdDb, -3.0f);
    EXPECT_LT(shape.heightDb, 0.01f);
}

TEST(DynamicsMigration, ACompressorThatDidNothingStillDoesNothing)
{
    const dsp::DynamicsShape shape =
        shapeOf(dynamicsParamsFromOld(QJsonObject{{"threshold"_L1, 0.0}, {"ratio"_L1, 1.0}, {"knee"_L1, 0.0}}));
    for (const float inputDb : {-60.0f, -20.0f, 0.0f, 20.0f}) {
        EXPECT_FLOAT_EQ(dsp::outputDb(inputDb, shape), inputDb);
    }
}

TEST(DynamicsMigration, AGateStaysAGate)
{
    const dsp::DynamicsShape shape = shapeOf(dynamicsParamsFromOld(QJsonObject{{"threshold"_L1, 0.0},
                                                                               {"ratio"_L1, 1.0},
                                                                               {"knee"_L1, 0.0},
                                                                               {"expanderThreshold"_L1, -45.0},
                                                                               {"expanderRatio"_L1, 20.0},
                                                                               {"expanderRange"_L1, 60.0}}));
    EXPECT_FLOAT_EQ(shape.expanderThresholdDb, -45.0f);
    EXPECT_FLOAT_EQ(shape.expanderRatio, 20.0f);
}

TEST(DynamicsMigration, AnExpanderWithoutARangeNeverDidAnything)
{
    // A ratio with its range at nothing bent nothing, and it was the range
    // that said so. The line is gone with it, and the point goes back to
    // where a resting expander waits.
    const dsp::DynamicsShape shape =
        shapeOf(dynamicsParamsFromOld(QJsonObject{{"expanderThreshold"_L1, -50.0}, {"expanderRatio"_L1, 4.0}}));
    EXPECT_FLOAT_EQ(shape.expanderRatio, 1.0f);
    EXPECT_FLOAT_EQ(shape.expanderThresholdDb, -80.0f);
}

TEST(DynamicsMigration, AShallowRangeBecomesAGentlerLineAndNotASilence)
{
    // 6 dB off, at 20:1: gently ducking the pauses. The steepness alone would
    // shut them altogether.
    const dsp::DynamicsShape shape = shapeOf(dynamicsParamsFromOld(
        QJsonObject{{"expanderThreshold"_L1, -50.0}, {"expanderRatio"_L1, 20.0}, {"expanderRange"_L1, 6.0}}));
    EXPECT_FLOAT_EQ(shape.expanderRatio, 3.0f);
    EXPECT_NEAR(dsp::gainReductionDb(-53.0f, shape), -6.0f, 0.01f);

    // A range that was reached slowly keeps its ratio.
    const dsp::DynamicsShape voice = shapeOf(dynamicsParamsFromOld(
        QJsonObject{{"expanderThreshold"_L1, -45.0}, {"expanderRatio"_L1, 2.0}, {"expanderRange"_L1, 12.0}}));
    EXPECT_FLOAT_EQ(voice.expanderRatio, 2.0f);
}

TEST(DynamicsMigration, TheThresholdsComeOutInOrder)
{
    const dsp::DynamicsShape shape = shapeOf(dynamicsParamsFromOld(QJsonObject{{"threshold"_L1, -40.0},
                                                                               {"knee"_L1, 0.0},
                                                                               {"expanderThreshold"_L1, -20.0},
                                                                               {"expanderRatio"_L1, 2.0},
                                                                               {"expanderRange"_L1, 12.0}}));
    EXPECT_LE(shape.expanderThresholdDb, shape.thresholdDb);
}

TEST(DynamicsMigration, WhatIsNotAboutTheCurveStaysAsItWas)
{
    const QJsonObject migrated = dynamicsParamsFromOld(QJsonObject{{"ratio"_L1, 4.0},
                                                                   {"attack"_L1, 5.0},
                                                                   {"release"_L1, 120.0},
                                                                   {"makeup"_L1, 4.0},
                                                                   {"expanderHold"_L1, 50.0},
                                                                   {"detector"_L1, u"rms"_s}});
    EXPECT_DOUBLE_EQ(migrated.value("attack"_L1).toDouble(), 5.0);
    EXPECT_DOUBLE_EQ(migrated.value("release"_L1).toDouble(), 120.0);
    EXPECT_DOUBLE_EQ(migrated.value("makeup"_L1).toDouble(), 4.0);
    EXPECT_DOUBLE_EQ(migrated.value("expanderHold"_L1).toDouble(), 50.0);
    EXPECT_EQ(migrated.value("detector"_L1).toString(), u"rms"_s);
    EXPECT_FALSE(holdsOldDynamicsParams(migrated)) << "an old parameter survived";
}

TEST(DynamicsMigration, EveryOldNumberGivesSomethingTheBlockTakes)
{
    // Every migrated value has to be one the block accepts, or the session
    // would quietly fall back to a default.
    for (const double threshold : {-80.0, -40.0, 0.0}) {
        for (const double ratio : {1.0, 1.5, 4.0, 20.0}) {
            for (const double knee : {0.0, 12.0, 24.0}) {
                for (const double expanderRatio : {1.0, 2.0, 50.0}) {
                    for (const double range : {0.0, 3.0, 80.0}) {
                        const QJsonObject migrated =
                            dynamicsParamsFromOld(QJsonObject{{"threshold"_L1, threshold},
                                                              {"ratio"_L1, ratio},
                                                              {"knee"_L1, knee},
                                                              {"expanderThreshold"_L1, -30.0},
                                                              {"expanderRatio"_L1, expanderRatio},
                                                              {"expanderRange"_L1, range}});
                        blocks::Dynamics block;
                        for (const char *id : {"threshold", "height", "expanderThreshold", "expanderRatio"}) {
                            const QJsonValue value = migrated.value(QLatin1StringView{id});
                            EXPECT_TRUE(
                                block.setParam(id, dsp::ParamValue::ofFloat(static_cast<float>(value.toDouble())))
                                    .has_value())
                                << id << " = " << value.toDouble();
                        }
                    }
                }
            }
        }
    }
}

TEST(DynamicsMigration, AnOldSessionOpensAsTheNearestShape)
{
    const QJsonObject oldBlock{
        {"id"_L1, u"d"_s},
        {"type"_L1, u"effect.dynamics"_s},
        {"params"_L1, QJsonObject{{"threshold"_L1, -18.0}, {"ratio"_L1, 2.0}, {"knee"_L1, 12.0}, {"attack"_L1, 20.0}}}};
    // A parameter of the same name on another kind of block is not this one.
    const QJsonObject otherBlock{
        {"id"_L1, u"m"_s}, {"type"_L1, u"effect.simplecompressor"_s}, {"params"_L1, QJsonObject{{"ratio"_L1, 3.0}}}};
    const QJsonObject file{{"schemaVersion"_L1, 1},
                           {"id"_L1, QUuid::createUuid().toString(QUuid::WithoutBraces)},
                           {"blocks"_L1, QJsonArray{oldBlock, otherBlock}}};

    const std::expected<Session, SessionError> session = sessionFromJson(file);
    ASSERT_TRUE(session.has_value());
    const BlockInstance *dynamics = session->findBlock(u"d"_s);
    ASSERT_NE(dynamics, nullptr);
    EXPECT_FALSE(holdsOldDynamicsParams(dynamics->params));
    EXPECT_DOUBLE_EQ(dynamics->params.value("threshold"_L1).toDouble(), -24.0);
    EXPECT_TRUE(dynamics->params.contains("height"_L1));
    EXPECT_DOUBLE_EQ(dynamics->params.value("attack"_L1).toDouble(), 20.0);

    const BlockInstance *other = session->findBlock(u"m"_s);
    ASSERT_NE(other, nullptr);
    EXPECT_DOUBLE_EQ(other->params.value("ratio"_L1).toDouble(), 3.0);

    // **Once**: saved, it is a new session and comes back unchanged.
    const std::expected<Session, SessionError> again = sessionFromJson(sessionToJson(*session));
    ASSERT_TRUE(again.has_value());
    EXPECT_EQ(again->findBlock(u"d"_s)->params, dynamics->params);
}

TEST(DynamicsMigration, APresetSavedBeforeTheChangeStillWorks)
{
    const QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ASSERT_TRUE(QDir(directory.path()).mkpath(u"effect.dynamics"_s));
    QFile file(directory.path() + "/effect.dynamics/Mine"_L1 + kPresetFileSuffix);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(QJsonDocument(QJsonObject{{"schema"_L1, 1},
                                         {"type"_L1, u"effect.dynamics"_s},
                                         {"params"_L1, QJsonObject{{"threshold"_L1, -18.0}, {"ratio"_L1, 2.0}}}})
                   .toJson());
    file.close();

    const PresetStore store(directory.path());
    const std::expected<Preset, PresetError> preset = store.preset(u"effect.dynamics"_s, u"Mine"_s);
    ASSERT_TRUE(preset.has_value());
    EXPECT_FALSE(holdsOldDynamicsParams(preset->params));
    EXPECT_TRUE(preset->params.contains("height"_L1));
}

TEST(DynamicsMigration, TheShippedPresetsDoWhatTheyAreCalled)
{
    const QList<Preset> shipped = PresetStore::factoryPresetsFor(u"effect.dynamics"_s);
    ASSERT_EQ(shipped.size(), 5);
    const auto find = [&shipped](const QString &needle) {
        const auto found = std::ranges::find_if(
            shipped, [&needle](const Preset &preset) { return preset.id.contains(needle, Qt::CaseInsensitive); });
        EXPECT_NE(found, shipped.end()) << needle.toStdString();
        return shapeOf(found->params);
    };
    for (const Preset &preset : shipped) {
        EXPECT_FALSE(holdsOldDynamicsParams(preset.params)) << preset.id.toStdString();
    }

    // The default is what a new block is.
    const dsp::DynamicsShape standard = find(u"default"_s);
    EXPECT_EQ(standard, blocks::Dynamics{}.settings());

    // Gentle leveling takes a little off, and only from loud sound.
    const dsp::DynamicsShape gentle = find(u"gentle"_s);
    EXPECT_FLOAT_EQ(dsp::gainReductionDb(-30.0f, gentle), 0.0f);
    EXPECT_NEAR(dsp::gainReductionDb(0.0f, gentle), -9.0f, 1.5f);

    // A voice is held back when it is loud and let down in the pauses.
    const dsp::DynamicsShape voice = find(u"voice"_s);
    EXPECT_LT(dsp::gainReductionDb(-6.0f, voice), -2.0f);
    EXPECT_FLOAT_EQ(dsp::gainReductionDb(-30.0f, voice), 0.0f);
    EXPECT_LT(dsp::gainReductionDb(-60.0f, voice), -10.0f);

    // A noise gate is the expander and nothing else.
    const dsp::DynamicsShape gate = find(u"noisegate"_s);
    EXPECT_FLOAT_EQ(dsp::gainReductionDb(-20.0f, gate), 0.0f);
    EXPECT_FLOAT_EQ(dsp::gainReductionDb(0.0f, gate), 0.0f);
    EXPECT_FLOAT_EQ(dsp::outputDb(-50.0f, gate), dsp::kDynamicsFloorDb);

    // Hard limiting lets nothing past its threshold.
    const dsp::DynamicsShape limiter = find(u"hardlimiting"_s);
    EXPECT_FLOAT_EQ(limiter.heightDb, 0.0f);
    EXPECT_FLOAT_EQ(dsp::outputDb(30.0f, limiter), limiter.thresholdDb);
}

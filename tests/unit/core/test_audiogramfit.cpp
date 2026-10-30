// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// "Fit from audiogram": one ear's audiogram read from a block's
// parameters, fitted, and written back as the block's band settings in one
// edit, which is one step to undo.

#include "blocks/builtinblocks.h"
#include "core/audiogramfit.h"
#include "core/editcommand.h"
#include "core/engine.h"
#include "core/session.h"
#include "core/sessionstore.h"
#include "dsp/blockregistry.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

using namespace rb;
using namespace rb::core;
using namespace Qt::StringLiterals;

namespace
{

[[nodiscard]] std::size_t pointOf(int hz)
{
    for (std::size_t point = 0; point < dsp::kAudiogramPoints; ++point) {
        if (dsp::kAudiogramFrequenciesHz[point] == hz) {
            return point;
        }
    }
    ADD_FAILURE() << hz << " Hz is not an audiogram point";
    return 0;
}

/// A sloping loss with a gap and a band the ear did not hear, in dB HL.
[[nodiscard]] dsp::Audiogram sloping()
{
    dsp::Audiogram audiogram;
    audiogram.points[pointOf(250)] = {dsp::AudiogramMark::Heard, 20.0f, std::nullopt};
    audiogram.points[pointOf(1000)] = {dsp::AudiogramMark::Heard, 35.0f, 105.0f};
    audiogram.points[pointOf(2000)] = {dsp::AudiogramMark::Heard, 50.0f, std::nullopt};
    audiogram.points[pointOf(4000)] = {dsp::AudiogramMark::Heard, 65.0f, std::nullopt};
    audiogram.points[pointOf(8000)] = {dsp::AudiogramMark::NoResponse, 90.0f, std::nullopt};
    return audiogram;
}

const std::vector<double> kCentres{250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};

[[nodiscard]] BlockInstance blockWith(const dsp::Audiogram &audiogram)
{
    BlockInstance block;
    block.id = u"left"_s;
    block.type = u"effect.volume"_s;
    block.params = audiogramToParams(audiogram);
    return block;
}

[[nodiscard]] QString asJson(const QJsonObject &object)
{
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

[[nodiscard]] QString asJson(const QJsonArray &array)
{
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

} // namespace

TEST(AudiogramFit, AnAudiogramComesBackFromTheParametersItWasWrittenTo)
{
    const dsp::Audiogram audiogram = sloping();
    EXPECT_EQ(audiogramFromParams(audiogramToParams(audiogram)), audiogram);

    // A block never given any is the descriptors' default: nothing measured,
    // in dB HL. A unit or a mark this build does not know reads as that too.
    EXPECT_EQ(audiogramFromParams({}), dsp::Audiogram{});
    QJsonObject later = audiogramToParams(audiogram);
    later.insert(u"audiogramUnit"_s, u"somethingLater"_s);
    later.insert(u"audiogram1000"_s, u"somethingLater"_s);
    const dsp::Audiogram read = audiogramFromParams(later);
    EXPECT_EQ(read.unit, dsp::AudiogramUnit::DbHl);
    EXPECT_EQ(read.points[pointOf(1000)].mark, dsp::AudiogramMark::None);

    // "Too loud" counts only where it was said to be given.
    QJsonObject notGiven = audiogramToParams(audiogram);
    notGiven.insert(u"audiogram1000TooLoud"_s, false);
    EXPECT_FALSE(audiogramFromParams(notGiven).points[pointOf(1000)].tooLoudDb.has_value());
}

TEST(AudiogramFit, EveryBandSettingIsWrittenAndFittingAgainChangesNothing)
{
    const BlockInstance block = blockWith(sloping());
    const dsp::FittingOptions options = dsp::defaultFittingOptions(dsp::AudiogramUnit::DbHl);
    const AudiogramFit fit = fitFromAudiogram(block, kCentres, options);
    ASSERT_EQ(fit.bands.size(), kCentres.size());
    ASSERT_EQ(fit.commands.size(), 4 * kCentres.size());

    BlockInstance fitted = block;
    for (const EditCommand &command : fit.commands) {
        const auto *set = std::get_if<SetParam>(&command);
        ASSERT_NE(set, nullptr);
        EXPECT_EQ(set->blockId, u"left"_s);
        fitted.params.insert(set->key, set->value);
    }
    EXPECT_NEAR(
        fitted.params.value(bandParamId(3, BandSetting::AverageGainDb)).toDouble(), fit.bands[3].averageGainDb, 0.05);
    EXPECT_NEAR(fitted.params.value(bandParamId(5, BandSetting::CeilingDb)).toDouble(), fit.bands[5].ceilingDb, 0.05);
    EXPECT_TRUE(fit.bands[5].noResponse) << "8 kHz was not heard";

    EXPECT_TRUE(fitFromAudiogram(fitted, kCentres, options).commands.empty())
        << "fitting a block already fitted left something to apply";
}

TEST(AudiogramFit, BandSettingsHaveStableNames)
{
    EXPECT_EQ(bandParamId(0, BandSetting::SoftGainDb), u"band0SoftGainDb"_s);
    EXPECT_EQ(bandParamId(15, BandSetting::AverageGainDb), u"band15AverageGainDb"_s);
    EXPECT_EQ(bandParamId(63, BandSetting::LoudGainDb), u"band63LoudGainDb"_s);
    EXPECT_EQ(bandParamId(7, BandSetting::CeilingDb), u"band7CeilingDb"_s);
}

namespace
{

/// The real engine over a session store in a temporary folder, with no
/// PipeWire: an edit and its undo are the session model's, and what they
/// did is what the store then reads back.
class FitInTheEngine : public ::testing::Test
{
protected:
    void SetUp() override
    {
        application = std::make_unique<QCoreApplication>(argc, argv.data());
        ASSERT_TRUE(blocks::registerBuiltinBlocks(registry));
        ASSERT_TRUE(sessions.isValid());
        engine = std::make_unique<Engine>(registry, SessionStore{sessions.path()});
        store = std::make_unique<SessionStore>(sessions.path());
        QSignalSpy opened(engine.get(), &EngineFacade::sessionOpened);
        engine->createSession(u"Fitted"_s);
        ASSERT_EQ(opened.count(), 1);
        id = opened.at(0).at(0).toString();
    }

    void TearDown() override
    {
        engine.reset();
        application.reset();
    }

    /// The one block as the disk holds it now.
    [[nodiscard]] QJsonObject paramsOnDisk() const
    {
        engine->flushAutosave();
        const std::expected<Session, SessionLoadFailure> loaded = store->load(QUuid(id));
        if (!loaded || loaded->blocks.size() != 1) {
            ADD_FAILURE() << "the session did not load with its one block";
            return {};
        }
        return loaded->blocks.front().params;
    }

    int argc = 1;
    char name[8] = "fitting";
    std::array<char *, 2> argv{static_cast<char *>(name), nullptr};
    std::unique_ptr<QCoreApplication> application;
    dsp::BlockRegistry registry;
    QTemporaryDir sessions;
    std::unique_ptr<Engine> engine;
    std::unique_ptr<SessionStore> store;
    QString id;
};

} // namespace

TEST_F(FitInTheEngine, FittingIsOneEditAndOneUndoTakesAllOfItBack)
{
    const BlockInstance block = blockWith(sloping());
    engine->applyEdit(id, asJson(editCommandToJson(AddBlock{block, -1})));
    ASSERT_EQ(paramsOnDisk(), block.params);

    const AudiogramFit fit = fitFromAudiogram(block, kCentres, dsp::defaultFittingOptions(dsp::AudiogramUnit::DbHl));
    ASSERT_EQ(fit.commands.size(), 4 * kCentres.size());
    QSignalSpy refused(engine.get(), &EngineFacade::error);
    engine->applyEdit(id, asJson(editListJson(fit.commands)));
    ASSERT_EQ(refused.count(), 0) << refused.front().at(0).toString().toStdString();

    const QJsonObject fitted = paramsOnDisk();
    for (std::size_t band = 0; band < kCentres.size(); ++band) {
        EXPECT_TRUE(fitted.contains(bandParamId(band, BandSetting::SoftGainDb))) << band;
        EXPECT_TRUE(fitted.contains(bandParamId(band, BandSetting::CeilingDb))) << band;
    }

    // **One step.** A person who fitted and did not like it presses undo
    // once, and the block is as it was: its audiogram, and no band settings.
    engine->undo(id);
    const QJsonObject undone = paramsOnDisk();
    qsizetype left = 0;
    for (std::size_t band = 0; band < kCentres.size(); ++band) {
        for (const BandSetting setting :
             {BandSetting::SoftGainDb, BandSetting::AverageGainDb, BandSetting::LoudGainDb, BandSetting::CeilingDb}) {
            left += undone.contains(bandParamId(band, setting)) ? 1 : 0;
        }
    }
    EXPECT_EQ(left, 0) << left << " of " << fit.commands.size() << " band settings were left after one undo";
    EXPECT_EQ(undone, block.params);

    // And one redo puts all of it back.
    engine->redo(id);
    EXPECT_EQ(paramsOnDisk(), fitted);
}

TEST(AudiogramFit, AHearingBlockIsFittedOnItsOwnBandsAndScale)
{
    EXPECT_EQ(hearingBandCount(u"hearing.compensation16"_s), 16U);
    EXPECT_EQ(hearingBandCount(u"effect.volume"_s), 0U);

    BlockInstance block = blockWith(sloping());
    block.type = u"hearing.compensation16"_s;
    const std::optional<AudiogramFit> fit = fitHearingBlock(block);
    ASSERT_TRUE(fit.has_value());
    ASSERT_EQ(fit->bands.size(), 16U);
    ASSERT_EQ(fit->commands.size(), 64U);

    // **A dBFS audiogram's ceilings land on the block's listening scale.**
    // Too loud at -10 dBFS everywhere: a ceiling 5 dB below that, -15 dBFS,
    // which is 70 on a scale kListeningOffsetDb above dBFS.
    dsp::Audiogram measured;
    measured.unit = dsp::AudiogramUnit::DbFs;
    for (dsp::AudiogramPoint &point : measured.points) {
        point = {dsp::AudiogramMark::Heard, -70.0f, -10.0f};
    }
    BlockInstance throughHeadphones = blockWith(measured);
    throughHeadphones.type = u"hearing.compensation16"_s;
    const std::optional<AudiogramFit> dbfs = fitHearingBlock(throughHeadphones);
    ASSERT_TRUE(dbfs.has_value());
    BlockInstance fitted = throughHeadphones;
    for (const EditCommand &command : dbfs->commands) {
        const auto *set = std::get_if<SetParam>(&command);
        ASSERT_NE(set, nullptr);
        fitted.params.insert(set->key, set->value);
    }
    for (std::size_t band = 0; band < 16; ++band) {
        EXPECT_NEAR(fitted.params.value(bandParamId(band, BandSetting::CeilingDb)).toDouble(), 70.0, 0.05) << band;
    }

    BlockInstance other = block;
    other.type = u"effect.volume"_s;
    EXPECT_FALSE(fitHearingBlock(other).has_value());
}

TEST(AudiogramFit, NoBandIsFittedAboveTheBlocksMostGainNorAboveTheRulesLimit)
{
    // A loss of 90 dB HL everywhere asks the rule for more than 50 dB of
    // soft gain in every band.
    dsp::Audiogram deep;
    for (dsp::AudiogramPoint &point : deep.points) {
        point = {dsp::AudiogramMark::Heard, 90.0f, std::nullopt};
    }
    const auto highest = [](const AudiogramFit &fit) {
        float most = 0.0f;
        for (const dsp::BandFit &band : fit.bands) {
            most = std::max({most, band.softGainDb, band.averageGainDb, band.loudGainDb});
        }
        return most;
    };

    for (const QString &type : {u"hearing.compensation16"_s, u"hearing.compensation64"_s}) {
        BlockInstance block = blockWith(deep);
        block.type = type;
        block.params.insert(u"maximumGainDb"_s, 30.0);
        const std::optional<AudiogramFit> held = fitHearingBlock(block);
        ASSERT_TRUE(held.has_value());
        EXPECT_FLOAT_EQ(highest(*held), 30.0f) << type.toStdString() << ": the fit went past Most gain";
        for (const dsp::BandFit &band : held->bands) {
            EXPECT_TRUE(band.beyondMaximum) << "a band that asked for more than Most gain is not marked";
        }

        // Most gain above the rule's own limit: the limit still holds.
        block.params.insert(u"maximumGainDb"_s, 60.0);
        const std::optional<AudiogramFit> limited = fitHearingBlock(block);
        ASSERT_TRUE(limited.has_value());
        EXPECT_FLOAT_EQ(highest(*limited), dsp::FittingOptions{}.maximumGainDb) << type.toStdString();

        // And a new block, whose Most gain the session does not name.
        block.params.remove(u"maximumGainDb"_s);
        const std::optional<AudiogramFit> fresh = fitHearingBlock(block);
        ASSERT_TRUE(fresh.has_value());
        EXPECT_FLOAT_EQ(highest(*fresh), 50.0f) << type.toStdString();
    }
}

TEST_F(FitInTheEngine, FittingAHearingBlockAndUndoingItRestoresEveryBand)
{
    BlockInstance block = blockWith(sloping());
    block.type = u"hearing.compensation16"_s;
    engine->applyEdit(id, asJson(editCommandToJson(AddBlock{block, -1})));
    const QJsonObject before = paramsOnDisk();

    const std::optional<AudiogramFit> fit = fitHearingBlock(block);
    ASSERT_TRUE(fit.has_value());
    QSignalSpy refused(engine.get(), &EngineFacade::error);
    engine->applyEdit(id, asJson(editListJson(fit->commands)));
    ASSERT_EQ(refused.count(), 0) << refused.front().at(0).toString().toStdString();

    const QJsonObject fitted = paramsOnDisk();
    int changed = 0;
    for (std::size_t band = 0; band < 16; ++band) {
        for (const BandSetting setting :
             {BandSetting::SoftGainDb, BandSetting::AverageGainDb, BandSetting::LoudGainDb, BandSetting::CeilingDb}) {
            changed += fitted.value(bandParamId(band, setting)) != before.value(bandParamId(band, setting)) ? 1 : 0;
        }
    }
    EXPECT_EQ(changed, 64) << "every band setting of every band was fitted";

    engine->undo(id);
    EXPECT_EQ(paramsOnDisk(), before) << "one undo put every band back";
}

TEST(AudiogramFit, ABlockSetThroughPointsGetsAPointAtEachMeasuredFrequency)
{
    // Fit from audiogram places points at the audiogram's frequencies.
    EXPECT_EQ(hearingBandCount(u"hearing.compensation64"_s), 64U);
    EXPECT_TRUE(hearingBlockHasPoints(u"hearing.compensation64"_s));
    EXPECT_FALSE(hearingBlockHasPoints(u"hearing.compensation16"_s));
    EXPECT_EQ(pointOnParamId(3), u"point3On"_s);
    EXPECT_EQ(pointHzParamId(23), u"point23Hz"_s);
    EXPECT_EQ(pointParamId(0, BandSetting::SoftGainDb), u"point0SoftGainDb"_s);
    EXPECT_EQ(pointParamId(7, BandSetting::CeilingDb), u"point7CeilingDb"_s);

    BlockInstance block = blockWith(sloping());
    block.type = u"hearing.compensation64"_s;
    const std::optional<AudiogramFit> fit = fitHearingBlock(block);
    ASSERT_TRUE(fit.has_value());
    // The five frequencies the audiogram has a level for, the one with no
    // response among them, low to high.
    const std::vector<double> measured{250.0, 1000.0, 2000.0, 4000.0, 8000.0};
    EXPECT_EQ(fit->centresHz, measured);
    ASSERT_EQ(fit->bands.size(), measured.size());
    EXPECT_TRUE(fit->bands.back().noResponse);
    // Fitted as bands at those frequencies would be.
    const dsp::FittingOptions options = dsp::defaultFittingOptions(dsp::AudiogramUnit::DbHl);
    const std::vector<dsp::BandFit> same = dsp::fitAudiogram(sloping(), measured, options);

    BlockInstance fitted = block;
    for (const EditCommand &command : fit->commands) {
        const auto *set = std::get_if<SetParam>(&command);
        ASSERT_NE(set, nullptr);
        fitted.params.insert(set->key, set->value);
    }
    for (std::size_t point = 0; point < kHearingPoints; ++point) {
        const bool used = point < measured.size();
        EXPECT_EQ(fitted.params.value(pointOnParamId(point)).toBool(), used) << point;
        if (!used) {
            continue;
        }
        EXPECT_DOUBLE_EQ(fitted.params.value(pointHzParamId(point)).toDouble(), measured[point]);
        EXPECT_NEAR(fitted.params.value(pointParamId(point, BandSetting::AverageGainDb)).toDouble(),
                    same[point].averageGainDb,
                    0.05);
        EXPECT_NEAR(
            fitted.params.value(pointParamId(point, BandSetting::CeilingDb)).toDouble(), same[point].ceilingDb, 0.05);
    }
    // Nothing is a band setting.
    EXPECT_FALSE(fitted.params.contains(bandParamId(0, BandSetting::SoftGainDb)));

    // Fitting again changes nothing.
    BlockInstance again = fitted;
    const std::optional<AudiogramFit> twice = fitHearingBlock(again);
    ASSERT_TRUE(twice.has_value());
    EXPECT_TRUE(twice->commands.empty()) << twice->commands.size() << " settings changed the second time";

    // An audiogram with nothing measured: nothing to fit.
    BlockInstance empty = blockWith(dsp::Audiogram{});
    empty.type = u"hearing.compensation64"_s;
    const std::optional<AudiogramFit> none = fitHearingBlock(empty);
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->commands.empty());
}

TEST_F(FitInTheEngine, FittingABlockSetThroughPointsAndUndoingItRestoresEveryPoint)
{
    BlockInstance block = blockWith(sloping());
    block.type = u"hearing.compensation64"_s;
    engine->applyEdit(id, asJson(editCommandToJson(AddBlock{block, -1})));
    const QJsonObject before = paramsOnDisk();

    const std::optional<AudiogramFit> fit = fitHearingBlock(block);
    ASSERT_TRUE(fit.has_value());
    QSignalSpy refused(engine.get(), &EngineFacade::error);
    engine->applyEdit(id, asJson(editListJson(fit->commands)));
    ASSERT_EQ(refused.count(), 0) << refused.front().at(0).toString().toStdString();

    const QJsonObject fitted = paramsOnDisk();
    int placed = 0;
    for (std::size_t point = 0; point < kHearingPoints; ++point) {
        placed += fitted.value(pointOnParamId(point)).toBool() ? 1 : 0;
    }
    EXPECT_EQ(placed, 5) << "a point at each of the five measured frequencies";
    EXPECT_EQ(fitted.value(pointHzParamId(4)).toDouble(), 8000.0);

    engine->undo(id);
    EXPECT_EQ(paramsOnDisk(), before) << "one undo put every point back";
}

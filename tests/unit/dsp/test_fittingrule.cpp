// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// One ear's audiogram and the rule that turns it into the listening blocks'
// band settings: the parameters it is kept in, how the points in
// between are worked out, and what the rule gives for the standard
// audiograms, kept as reviewed tables in tests/unit/dsp/fitting/.
//
// **To write the tables again** after a deliberate change to the rule, run
// the test with RB_UPDATE_FITTING_TABLES=1 and review the diff before
// committing it; that diff is the change a listener would hear.

#include "dsp/audiogram.h"
#include "dsp/fittingrule.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace rb::dsp;

namespace
{

/// The frequencies of the standard audiograms, in Hz; 375 Hz, which they
/// also give, is not one of our points and is left out.
constexpr std::array<int, 9> kStandardFrequenciesHz{250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000};

/// The standard audiograms of Bisgaard, Vlaming & Dahlquist (2010),
/// "Standard audiograms for the IEC 60118-15 measurement procedure", Trends
/// in Amplification 14(2), 113–120, table 1: seven flat or gently sloping
/// (N1–N7) and three steeply sloping (S1–S3), in dB HL, at
/// kStandardFrequenciesHz.
const std::vector<std::pair<std::string, std::array<float, 9>>> kStandardAudiograms{
    {"N1", {10, 10, 10, 10, 10, 15, 20, 30, 40}},
    {"N2", {20, 20, 22.5f, 25, 30, 35, 40, 45, 50}},
    {"N3", {35, 35, 35, 40, 45, 50, 55, 60, 65}},
    {"N4", {55, 55, 55, 55, 60, 65, 70, 75, 80}},
    {"N5", {65, 70, 72.5f, 75, 80, 80, 80, 80, 80}},
    {"N6", {75, 80, 82.5f, 85, 90, 90, 95, 100, 100}},
    {"N7", {90, 95, 100, 105, 105, 105, 105, 105, 105}},
    {"S1", {10, 10, 10, 10, 10, 15, 30, 55, 70}},
    {"S2", {20, 20, 22.5f, 25, 35, 55, 75, 95, 95}},
    {"S3", {30, 35, 47.5f, 60, 70, 75, 80, 80, 85}},
};

[[nodiscard]] std::size_t pointOf(int hz)
{
    for (std::size_t point = 0; point < kAudiogramPoints; ++point) {
        if (kAudiogramFrequenciesHz[point] == hz) {
            return point;
        }
    }
    ADD_FAILURE() << hz << " Hz is not an audiogram point";
    return 0;
}

[[nodiscard]] Audiogram standard(const std::array<float, 9> &levels)
{
    Audiogram audiogram;
    for (std::size_t index = 0; index < kStandardFrequenciesHz.size(); ++index) {
        AudiogramPoint &point = audiogram.points[pointOf(kStandardFrequenciesHz[index])];
        point.mark = AudiogramMark::Heard;
        point.levelDb = levels[index];
    }
    return audiogram;
}

[[nodiscard]] Audiogram flat(float levelDb)
{
    Audiogram audiogram;
    for (AudiogramPoint &point : audiogram.points) {
        point.mark = AudiogramMark::Heard;
        point.levelDb = levelDb;
    }
    return audiogram;
}

/// Band centres spread evenly in octaves from \a lowHz to \a highHz: a
/// stand-in for ADR-0019's layouts, which the rule must not depend on.
[[nodiscard]] std::vector<double> centres(std::size_t count, double lowHz = 100.0, double highHz = 16000.0)
{
    std::vector<double> hz;
    for (std::size_t band = 0; band < count; ++band) {
        const double along = count == 1 ? 0.0 : static_cast<double>(band) / static_cast<double>(count - 1);
        hz.push_back(lowHz * std::pow(highHz / lowHz, along));
    }
    return hz;
}

/// The audiogram's own frequencies, as the band centres the tables use: the
/// numbers a reader can check against the audiogram they came from.
[[nodiscard]] std::vector<double> audiogramCentres()
{
    return {kAudiogramFrequenciesHz.begin(), kAudiogramFrequenciesHz.end()};
}

/// What a band puts out for sound at \a inputDb, given its gain there.
constexpr float kTolerance = 1e-4f;

void expectOrderedAndUnderTheCeiling(const std::vector<BandFit> &bands,
                                     const FittingOptions &options,
                                     const std::string &what)
{
    const float average = options.averageLevelDb;
    const float soft = average - options.levelStepDb;
    const float loud = average + options.levelStepDb;
    for (std::size_t band = 0; band < bands.size(); ++band) {
        const BandFit &fit = bands[band];
        EXPECT_GE(fit.softGainDb + kTolerance, fit.averageGainDb) << what << ", band " << band;
        EXPECT_GE(fit.averageGainDb + kTolerance, fit.loudGainDb) << what << ", band " << band;
        EXPECT_LE(soft + fit.softGainDb, fit.ceilingDb + kTolerance) << what << ", band " << band;
        EXPECT_LE(average + fit.averageGainDb, fit.ceilingDb + kTolerance) << what << ", band " << band;
        EXPECT_LE(loud + fit.loudGainDb, fit.ceilingDb + kTolerance) << what << ", band " << band;
        EXPECT_LE(fit.softGainDb, options.maximumGainDb + kTolerance) << what << ", band " << band;
    }
}

/// One table: every standard audiogram's result at the audiogram's own
/// frequencies, one line a band, to a tenth of a decibel.
[[nodiscard]] std::string table(FittingRule rule)
{
    std::ostringstream out;
    // The table's own licence header, not this file's.
    // REUSE-IgnoreStart
    out << "# SPDX-FileCopyrightText: 2026 Reverblabr contributors\n"
        << "# SPDX-License-Identifier: CC0-1.0\n"
        // REUSE-IgnoreEnd
        << "#\n"
        << "# What the " << fittingRuleName(rule) << " rule gives the standard audiograms, in dB.\n"
        << "# Written by test_fittingrule.cpp with RB_UPDATE_FITTING_TABLES=1; review the diff.\n"
        << "# audiogram\thz\tlevel\tsoft\taverage\tloud\tceiling\tflags\n";
    FittingOptions options = defaultFittingOptions(AudiogramUnit::DbHl);
    options.rule = rule;
    for (const auto &[name, levels] : kStandardAudiograms) {
        const Audiogram audiogram = standard(levels);
        const std::vector<double> hz = audiogramCentres();
        const std::vector<BandFit> bands = fitAudiogram(audiogram, hz, options);
        for (std::size_t band = 0; band < bands.size(); ++band) {
            const BandFit &fit = bands[band];
            char line[160];
            std::snprintf(line,
                          sizeof line,
                          "%s\t%.0f\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f\t%s%s\n",
                          name.c_str(),
                          hz[band],
                          static_cast<double>(*audiogramLevelAt(audiogram, hz[band])),
                          static_cast<double>(fit.softGainDb),
                          static_cast<double>(fit.averageGainDb),
                          static_cast<double>(fit.loudGainDb),
                          static_cast<double>(fit.ceilingDb),
                          fit.noResponse ? "noResponse " : "",
                          fit.beyondMaximum ? "beyondMaximum" : "-");
            out << line;
        }
    }
    return out.str();
}

[[nodiscard]] std::string tablePath(FittingRule rule)
{
    return std::string{RB_FITTING_TABLES_DIR} + "/" + std::string{fittingRuleName(rule)} + ".tsv";
}

void checkTable(FittingRule rule)
{
    const std::string expected = table(rule);
    const std::string path = tablePath(rule);
    if (std::getenv("RB_UPDATE_FITTING_TABLES") != nullptr) {
        std::ofstream(path) << expected;
        GTEST_SKIP() << "wrote " << path << "; review the diff";
    }
    std::ifstream file(path);
    ASSERT_TRUE(file.is_open()) << path << " is missing; run with RB_UPDATE_FITTING_TABLES=1 and review it";
    const std::string reviewed{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_EQ(reviewed, expected) << "the rule no longer gives what " << path
                                  << " says. If that is meant, write it again with RB_UPDATE_FITTING_TABLES=1 "
                                     "and review the diff.";
}

} // namespace

TEST(Audiogram, ItsParametersAreValidHiddenAndAutomatable)
{
    const std::vector<ParamDescriptor> params = audiogramParams();
    ASSERT_TRUE(validateParams(params).has_value());
    EXPECT_EQ(params.size(), 1 + (4 * kAudiogramPoints));
    for (const ParamDescriptor &param : params) {
        EXPECT_TRUE(param.flags.hidden) << param.id << ": the panel draws its own audiogram editor";
        // A number or a choice the session plan never reads: editing the
        // audiogram must never make the session be planned again.
        EXPECT_TRUE(param.flags.automatable) << param.id;
        EXPECT_FALSE(param.flags.readOnly) << param.id;
    }
    // Stable for ever: a session file holds them.
    EXPECT_EQ(params.front().id, "audiogramUnit");
    EXPECT_EQ(audiogramParamId(pointOf(1500), AudiogramField::Mark), "audiogram1500");
    EXPECT_EQ(audiogramParamId(pointOf(1500), AudiogramField::LevelDb), "audiogram1500Db");
    EXPECT_EQ(audiogramParamId(pointOf(125), AudiogramField::TooLoudGiven), "audiogram125TooLoud");
    EXPECT_EQ(audiogramParamId(pointOf(8000), AudiogramField::TooLoudDb), "audiogram8000TooLoudDb");
}

TEST(Audiogram, APointInBetweenIsOnAStraightLineInLogFrequencyAndTheEndsCarryOn)
{
    Audiogram audiogram;
    EXPECT_FALSE(audiogramLevelAt(audiogram, 1000.0).has_value()) << "nothing measured, nothing to say";
    EXPECT_TRUE(audiogram.isEmpty());

    audiogram.points[pointOf(1000)] = {AudiogramMark::Heard, 20.0f, std::nullopt};
    audiogram.points[pointOf(4000)] = {AudiogramMark::NoResponse, 60.0f, std::nullopt};
    EXPECT_FALSE(audiogram.isEmpty());

    EXPECT_NEAR(*audiogramLevelAt(audiogram, 2000.0), 40.0f, 1e-4f) << "halfway in octaves, not in hertz";
    EXPECT_NEAR(*audiogramLevelAt(audiogram, 1000.0), 20.0f, 1e-4f);
    EXPECT_NEAR(*audiogramLevelAt(audiogram, 125.0), 20.0f, 1e-4f) << "below the lowest point it carries on";
    EXPECT_NEAR(*audiogramLevelAt(audiogram, 12000.0), 60.0f, 1e-4f) << "above the highest point it carries on";

    EXPECT_EQ(nearestMeasuredPoint(audiogram, 1900.0), pointOf(1000));
    EXPECT_EQ(nearestMeasuredPoint(audiogram, 2100.0), pointOf(4000));
    EXPECT_FALSE(audiogramTooLoudAt(audiogram, 1000.0).has_value());
    audiogram.points[pointOf(1000)].tooLoudDb = 95.0f;
    EXPECT_NEAR(*audiogramTooLoudAt(audiogram, 3000.0), 95.0f, 1e-4f);
}

TEST(FittingRule, NalRIsTheFormulaAsPublished)
{
    // N3 at 1 kHz by hand: 0.31 × 40 + 0.05 × (35 + 40 + 50) + 1.
    EXPECT_NEAR(averageGainDb(FittingRule::NalR, 1000.0, 40.0f, 35.0f, 40.0f, 50.0f), 19.65f, 1e-3f);
    // At 250 Hz: 0.31 × 35 + 6.25 − 17.
    EXPECT_NEAR(averageGainDb(FittingRule::NalR, 250.0, 35.0f, 35.0f, 40.0f, 50.0f), 0.1f, 1e-3f);
    // Below 250 Hz its constant carries on; halfway between 750 and 1000 Hz
    // in octaves, it is halfway between −3 and +1.
    EXPECT_NEAR(averageGainDb(FittingRule::NalR, 125.0, 60.0f, 0.0f, 0.0f, 0.0f), (0.31f * 60.0f) - 17.0f, 1e-3f);
    EXPECT_NEAR(averageGainDb(FittingRule::NalR, std::sqrt(750.0 * 1000.0), 60.0f, 0.0f, 0.0f, 0.0f),
                (0.31f * 60.0f) - 1.0f,
                1e-3f);
    EXPECT_NEAR(averageGainDb(FittingRule::HalfGain, 3000.0, 50.0f, 0.0f, 0.0f, 0.0f), 25.0f, 1e-4f);
}

TEST(FittingRule, AnEarWithoutLossIsGivenNoGainAndNoCompression)
{
    for (const FittingRule rule : {FittingRule::HalfGain, FittingRule::NalR}) {
        FittingOptions options = defaultFittingOptions(AudiogramUnit::DbHl);
        options.rule = rule;
        for (const Audiogram &audiogram : {flat(0.0f), Audiogram{}}) {
            for (const std::size_t count : {std::size_t{11}, std::size_t{16}, std::size_t{64}}) {
                const std::vector<BandFit> bands = fitAudiogram(audiogram, centres(count), options);
                ASSERT_EQ(bands.size(), count);
                for (std::size_t band = 0; band < count; ++band) {
                    EXPECT_EQ(bands[band].softGainDb, 0.0f) << fittingRuleName(rule) << ", band " << band;
                    EXPECT_EQ(bands[band].averageGainDb, 0.0f) << fittingRuleName(rule) << ", band " << band;
                    EXPECT_EQ(bands[band].loudGainDb, 0.0f) << fittingRuleName(rule) << ", band " << band;
                    EXPECT_FALSE(bands[band].noResponse);
                    EXPECT_FALSE(bands[band].beyondMaximum);
                }
            }
        }
    }
}

TEST(FittingRule, SoftIsLiftedMostAndNothingGoesAboveTheCeiling)
{
    for (const FittingRule rule : {FittingRule::HalfGain, FittingRule::NalR}) {
        FittingOptions options = defaultFittingOptions(AudiogramUnit::DbHl);
        options.rule = rule;
        for (const auto &[name, levels] : kStandardAudiograms) {
            const Audiogram audiogram = standard(levels);
            expectOrderedAndUnderTheCeiling(fitAudiogram(audiogram, centres(16), options),
                                            options,
                                            std::string{fittingRuleName(rule)} + " " + name + " at 16 bands");
            expectOrderedAndUnderTheCeiling(fitAudiogram(audiogram, centres(64), options),
                                            options,
                                            std::string{fittingRuleName(rule)} + " " + name + " at 64 bands");
        }

        // And audiograms nobody would draw: jagged, gaps, a "too loud"
        // level measured below the quietest level heard, marks of every
        // kind.
        std::mt19937 random(252);
        std::uniform_real_distribution<float> level(-10.0f, 120.0f);
        std::uniform_int_distribution<int> mark(0, 2);
        std::bernoulli_distribution given(0.3);
        for (int trial = 0; trial < 200; ++trial) {
            Audiogram audiogram;
            for (AudiogramPoint &point : audiogram.points) {
                point.mark = static_cast<AudiogramMark>(mark(random));
                point.levelDb = level(random);
                if (given(random)) {
                    point.tooLoudDb = level(random);
                }
            }
            expectOrderedAndUnderTheCeiling(fitAudiogram(audiogram, centres(64), options),
                                            options,
                                            std::string{fittingRuleName(rule)} + " trial " + std::to_string(trial));
        }
    }

    // A loss that sounds only just above its threshold on average is given
    // more for soft sound than for average: that is the compression.
    const FittingOptions options = defaultFittingOptions(AudiogramUnit::DbHl);
    const std::vector<BandFit> n4 = fitAudiogram(standard(kStandardAudiograms[3].second), centres(16), options);
    EXPECT_GT(n4[8].softGainDb, n4[8].averageGainDb + 1.0f);
}

TEST(FittingRule, NoResponseAndMoreThanTheMostAreReportedBandByBand)
{
    Audiogram audiogram = standard(kStandardAudiograms[8].second); // S2: steep above 2 kHz
    audiogram.points[pointOf(6000)].mark = AudiogramMark::NoResponse;
    audiogram.points[pointOf(6000)].levelDb = 110.0f;
    FittingOptions options = defaultFittingOptions(AudiogramUnit::DbHl);
    options.maximumGainDb = 30.0f;

    const std::vector<double> hz = audiogramCentres();
    const std::vector<BandFit> bands = fitAudiogram(audiogram, hz, options);
    std::set<int> noResponse;
    std::set<int> beyond;
    for (std::size_t band = 0; band < bands.size(); ++band) {
        if (bands[band].noResponse) {
            noResponse.insert(static_cast<int>(hz[band]));
        }
        if (bands[band].beyondMaximum) {
            beyond.insert(static_cast<int>(hz[band]));
        }
    }
    // 6 kHz, and 8 kHz, whose nearest measured point it is.
    EXPECT_EQ(noResponse, (std::set<int>{6000, 8000}));
    // S2 asks for more than 30 dB from 3 kHz up, and nowhere below 1.5 kHz.
    EXPECT_TRUE(beyond.contains(3000) && beyond.contains(4000) && beyond.contains(6000)) << beyond.size();
    EXPECT_FALSE(beyond.contains(250) || beyond.contains(500) || beyond.contains(1000)) << beyond.size();
    for (const BandFit &band : bands) {
        EXPECT_LE(band.softGainDb, 30.0f);
    }
}

TEST(FittingRule, TheStandardAudiogramsGiveTheReviewedTablesWithNalR)
{
    checkTable(FittingRule::NalR);
}

TEST(FittingRule, TheStandardAudiogramsGiveTheReviewedTablesWithHalfGain)
{
    checkTable(FittingRule::HalfGain);
}

TEST(FittingRule, RulesAndUnitsHaveStableNames)
{
    EXPECT_EQ(fittingRuleName(FittingRule::NalR), "nalR");
    EXPECT_EQ(fittingRuleName(FittingRule::HalfGain), "halfGain");
    EXPECT_EQ(fittingRuleFromName("halfGain"), FittingRule::HalfGain);
    EXPECT_FALSE(fittingRuleFromName("nal-nl2").has_value());
    EXPECT_EQ(audiogramUnitFromName("dbFs"), AudiogramUnit::DbFs);
    EXPECT_EQ(audiogramMarkFromName("noResponse"), AudiogramMark::NoResponse);
    EXPECT_FALSE(audiogramMarkFromName("deaf").has_value());
}

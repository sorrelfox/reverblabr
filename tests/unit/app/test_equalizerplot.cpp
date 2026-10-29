// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the Parametric EQ's panel and face draw, measured rather than
// looked at: that the line handed to the Shape is the response the block
// applies — a tone through the real block included — that writing the same
// bands again costs no rebuild, and that a band the session does not
// mention is the block's own default.

#include "analysis.h"
#include "blocks/parametriceq.h"
#include "core/telemetry.h"
#include "dsp/decibel.h"
#include "equalizerplot.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>

using namespace rb;
using namespace rb::ui;
using namespace rb::test;
using namespace Qt::StringLiterals;

namespace
{

/// A plot the size of the panel's.
[[nodiscard]] std::unique_ptr<EqualizerPlot> plotOf(const QVariantMap &params)
{
    auto plot = std::make_unique<EqualizerPlot>();
    plot->setPlotWidth(360.0);
    plot->setPlotHeight(180.0);
    plot->setParams(params);
    return plot;
}

/// The drawn line's height at \a x, followed between two samples as the
/// screen shows it.
[[nodiscard]] double drawnYAt(const EqualizerPlot &plot, double x)
{
    const QList<QPointF> points = plot.curve();
    for (qsizetype index = 1; index < points.size(); ++index) {
        const QPointF &before = points.at(index - 1);
        const QPointF &after = points.at(index);
        if (x >= before.x() && x <= after.x()) {
            const double span = after.x() - before.x();
            return before.y() + (span > 0.0 ? (x - before.x()) / span : 0.0) * (after.y() - before.y());
        }
    }
    return std::nan("");
}

/// Two bells and a shelf, as the session would hold them.
[[nodiscard]] QVariantMap shaped()
{
    return {
        {u"band1Frequency"_s, 250.0},
        {u"band1Gain"_s, 9.0},
        {u"band1Q"_s, 1.4},
        {u"band2Frequency"_s, 700.0},
        {u"band2Gain"_s, -6.0},
        {u"band2Q"_s, 3.0},
        {u"band3Type"_s, u"highShelf"_s},
        {u"band3Frequency"_s, 6000.0},
        {u"band3Gain"_s, -5.0},
        {u"band3Q"_s, 0.7},
    };
}

} // namespace

TEST(EqualizerPlotTest, AFlatEqualizerIsALineAcrossTheMiddle)
{
    const auto plot = plotOf({});
    ASSERT_GE(plot->curve().size(), 360);
    for (const QPointF &point : plot->curve()) {
        EXPECT_NEAR(point.y(), 90.0, 1e-9);
    }
    EXPECT_DOUBLE_EQ(plot->curve().constFirst().x(), 0.0);
    EXPECT_DOUBLE_EQ(plot->curve().constLast().x(), 360.0);
}

TEST(EqualizerPlotTest, TheDrawnLineIsTheBlocksResponse)
{
    // Every drawn point against the block's own cascade, at the frequency
    // the point stands for.
    const QVariantMap params = shaped();
    const auto plot = plotOf(params);
    blocks::ParametricEq block;
    for (auto value = params.constBegin(); value != params.constEnd(); ++value) {
        const std::string id = value.key().toStdString();
        const dsp::ParamValue parameter = value.value().typeId() == QMetaType::QString
            ? dsp::ParamValue::ofEnum(value.value().toString().toStdString())
            : dsp::ParamValue::ofFloat(value.value().toFloat());
        ASSERT_TRUE(block.setParam(id, parameter).has_value()) << id;
    }
    const auto bands = block.bands();
    for (const QPointF &point : plot->curve()) {
        const double expected = blocks::ParametricEq::curveDb(plot->hzAtX(point.x()), bands, 48000.0);
        EXPECT_NEAR(point.y(), plot->yOfDb(expected), 1e-6) << point.x();
    }

    // **And the sound, not only the design.** A tone through the real block
    // comes out as loud as the line says, where the line says it.
    BlockRunner runner(block, 2, 2);
    ASSERT_TRUE(runner.prepare().has_value());
    for (const double hz : {100.0, 250.0, 700.0, 2000.0, 9000.0}) {
        const MultiSignal out = runner.run(spread(sine(24000, hz, kTestSampleRate, 0.1f), 2));
        const Signal settled(out[0].begin() + 12000, out[0].end());
        const double heardDb = dsp::gainToDb(amplitudeAt(settled, hz) / 0.1f);
        EXPECT_NEAR(plot->dbAtY(drawnYAt(*plot, plot->xOfHz(hz))), heardDb, 0.1) << hz << " Hz";
    }
}

TEST(EqualizerPlotTest, TheSelectedBandIsDrawnOnItsOwnToo)
{
    auto plot = plotOf(shaped());
    EXPECT_TRUE(plot->bandCurve().isEmpty());
    plot->setSelectedBand(0);
    ASSERT_EQ(plot->bandCurve().size(), plot->curve().size());
    const QList<QPointF> band = plot->bandCurve();
    const auto top = std::ranges::min_element(band, {}, &QPointF::y);
    EXPECT_NEAR(plot->hzAtX(top->x()), 250.0, 10.0) << "the band's own line does not peak at its frequency";
    EXPECT_NEAR(plot->dbAtY(top->y()), 9.0, 0.05);
}

TEST(EqualizerPlotTest, WritingTheSameBandsAgainCostsNoRebuild)
{
    auto plot = plotOf(shaped());
    const qulonglong before = plot->tessellations();
    QVariantMap same = shaped();
    same.insert(u"band1On"_s, true); // already on: the response does not move
    plot->setParams(same);
    EXPECT_EQ(plot->tessellations(), before);
    same.insert(u"band1Gain"_s, 3.0);
    plot->setParams(same);
    EXPECT_EQ(plot->tessellations(), before + 1);
}

TEST(EqualizerPlotTest, WhatTheSessionDoesNotSayIsTheBlocksDefault)
{
    // Three bells at 0 dB: flat. A value the block would refuse costs that
    // one parameter, not the curve.
    auto plot = plotOf({{u"band1Gain"_s, 40.0}, {u"band2Gain"_s, 6.0}});
    EXPECT_NEAR(plot->bandDb(0, 100.0), 0.0, 1e-9) << "a refused gain was drawn";
    EXPECT_NEAR(plot->magnitudeDb(1000.0), 6.0, 1e-6);
    EXPECT_EQ(plot->paramId(9, u"Q"_s), u"band10Q"_s);
    EXPECT_EQ(plot->bandCount(), 10);
}

TEST(EqualizerPlotTest, TheAxesGoBothWaysAndStopAtTheirEnds)
{
    const auto plot = plotOf({});
    for (const double hz : {20.0, 63.0, 1000.0, 15000.0, 20000.0}) {
        EXPECT_NEAR(plot->hzAtX(plot->xOfHz(hz)), hz, hz * 1e-9);
    }
    // An octave is the same distance anywhere.
    EXPECT_NEAR(plot->xOfHz(200.0) - plot->xOfHz(100.0), plot->xOfHz(8000.0) - plot->xOfHz(4000.0), 1e-9);
    EXPECT_DOUBLE_EQ(plot->xOfHz(5.0), 0.0);
    EXPECT_DOUBLE_EQ(plot->xOfHz(40000.0), 360.0);
    EXPECT_DOUBLE_EQ(plot->yOfDb(24.0), 0.0);
    EXPECT_DOUBLE_EQ(plot->yOfDb(-24.0), 180.0);
    EXPECT_DOUBLE_EQ(plot->yOfDb(-60.0), 180.0);
    EXPECT_NEAR(plot->dbAtY(plot->yOfDb(-7.5)), -7.5, 1e-9);
}

TEST(EqualizerPlotTest, TheSpectrumIsPlacedBehindTheCurveOnItsOwnScale)
{
    // One level a band, at the band's centre across, on a scale of its
    // own up — the floor at the bottom, full scale at the top — and closed
    // along the bottom for the fill.
    auto plot = plotOf(shaped());
    EXPECT_TRUE(plot->spectrumArea().isEmpty());
    const qulonglong curveBuilds = plot->tessellations();
    QVariantList bands;
    for (std::size_t band = 0; band < rb::core::kSpectrumBands; ++band) {
        bands.append(band == 60 ? -12.0 : -90.0);
    }
    plot->setSpectrum(bands);
    const QList<QPointF> area = plot->spectrumArea();
    ASSERT_EQ(area.size(), static_cast<qsizetype>(rb::core::kSpectrumBands) + 2);
    EXPECT_EQ(area.constFirst(), QPointF(0.0, 180.0));
    EXPECT_EQ(area.constLast(), QPointF(360.0, 180.0));
    const QPointF loud = area.at(61);
    EXPECT_NEAR(loud.x(), plot->xOfHz(rb::core::spectrumBandCentreHz(60)), 1e-9);
    EXPECT_NEAR(loud.y(), 180.0 * 12.0 / 90.0, 1e-9);
    EXPECT_NEAR(area.at(1).y(), 180.0, 1e-9) << "the floor is not the bottom of the plot";
    // **Sixty of these a second cost the curve nothing.**
    EXPECT_EQ(plot->tessellations(), curveBuilds);
    plot->setSpectrum({});
    EXPECT_TRUE(plot->spectrumArea().isEmpty());
}

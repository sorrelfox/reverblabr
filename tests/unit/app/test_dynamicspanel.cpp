// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the Dynamics Processor's panel draws, measured rather than
// looked at: that the line handed to the Shape is the curve the block
// applies — all the way from the drawn points to a tone through the real
// block — that a moving marker costs no re-tessellation, and that a
// parameter the session does not mention is the block's own default.

#include "analysis.h"
#include "blocks/dynamics.h"
#include "dsp/decibel.h"
#include "dynamicsplot.h"
#include "offlinerunner.h"
#include "signals.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace rb;
using namespace rb::ui;
using namespace rb::test;
using namespace Qt::StringLiterals;

namespace
{

/// A plot the size of the panel's, with the whole range on it.
[[nodiscard]] std::unique_ptr<DynamicsPlot> plotOf(const QVariantMap &params)
{
    auto plot = std::make_unique<DynamicsPlot>();
    plot->setPlotWidth(272.0);
    plot->setPlotHeight(272.0);
    plot->setParams(params);
    return plot;
}

/// **What is drawn, not what could have been drawn.** The output level is
/// read back off the polyline the Shape is handed, by finding the segment
/// the input falls in and following it — which is what the screen shows
/// between two samples.
[[nodiscard]] double drawnOutputDb(const DynamicsPlot &plot, double inputDb)
{
    const QList<QPointF> points = plot.curve();
    EXPECT_GE(points.size(), 2);
    const double x = plot.xOfDb(inputDb);
    for (qsizetype index = 1; index < points.size(); ++index) {
        const QPointF &before = points.at(index - 1);
        const QPointF &after = points.at(index);
        if (x < before.x() || x > after.x()) {
            continue;
        }
        const double span = after.x() - before.x();
        const double along = span > 0.0 ? (x - before.x()) / span : 0.0;
        return plot.dbAtY(before.y() + along * (after.y() - before.y()));
    }
    return plot.dbAtY(points.constLast().y());
}

/// One setting, given to the panel and to the block at once, so that the
/// test cannot quietly draw one curve and measure another.
struct Setting {
    QString id;
    dsp::ParamValue value;
    QVariant asVariant;
};

[[nodiscard]] Setting number(const QString &id, float value)
{
    return Setting{id, dsp::ParamValue::ofFloat(value), QVariant(static_cast<double>(value))};
}

[[nodiscard]] Setting flag(const QString &id, bool value)
{
    return Setting{id, dsp::ParamValue::ofBool(value), QVariant(value)};
}

[[nodiscard]] QVariantMap mapOf(const std::vector<Setting> &settings)
{
    QVariantMap params;
    for (const Setting &setting : settings) {
        params.insert(setting.id, setting.asVariant);
    }
    return params;
}

void applyTo(blocks::Dynamics &block, const std::vector<Setting> &settings)
{
    for (const Setting &setting : settings) {
        const std::expected<void, dsp::BlockError> applied = block.setParam(setting.id.toStdString(), setting.value);
        EXPECT_TRUE(applied.has_value()) << "the block refused " << setting.id.toStdString();
    }
}

[[nodiscard]] MultiSignal stereoSine(std::size_t frames, float amplitude)
{
    MultiSignal signal;
    signal.push_back(sine(frames, 1000.0, kTestSampleRate, amplitude));
    signal.push_back(sine(frames, 1000.0, kTestSampleRate, amplitude));
    return signal;
}

/// The part of a run after the attack and the release have arrived, so that
/// a level is a level and not a glide.
[[nodiscard]] Signal settled(const Signal &signal)
{
    return Signal(signal.begin() + static_cast<Signal::difference_type>(24000), signal.end());
}

} // namespace

TEST(DynamicsPanelTest, TheDrawnLineIsTheBlocksOwnCurve)
{
    // The criterion: the polyline the Shape is handed is the curve the block
    // applies, within a tenth of a decibel at every decibel of the range.
    // **Half a decibel between samples** is finer than the knee bends, so
    // the straight line the screen draws between two of them is the curve.
    const std::vector<std::vector<Setting>> shapes{
        {number(u"threshold"_s, -24.0f), number(u"ratio"_s, 4.0f), number(u"knee"_s, 6.0f), number(u"makeup"_s, 3.0f)},
        {number(u"threshold"_s, -6.0f), number(u"ratio"_s, 20.0f), number(u"knee"_s, 24.0f)},
        {number(u"threshold"_s, -40.0f), flag(u"infiniteRatio"_s, true), number(u"knee"_s, 0.0f)},
        {number(u"expanderThreshold"_s, -50.0f),
         number(u"expanderRatio"_s, 3.0f),
         number(u"expanderRange"_s, 30.0f),
         number(u"threshold"_s, -12.0f),
         number(u"ratio"_s, 2.0f)},
    };

    for (const std::vector<Setting> &shape : shapes) {
        const std::unique_ptr<DynamicsPlot> plot = plotOf(mapOf(shape));
        for (double inputDb = plot->lowestDb(); inputDb <= plot->highestDb(); inputDb += 1.0) {
            EXPECT_NEAR(drawnOutputDb(*plot, inputDb), plot->outputDb(inputDb), 0.1) << "at " << inputDb << " dBFS in";
        }
    }
}

TEST(DynamicsPanelTest, TheDrawnLinePredictsWhatTheBlockDoesToASound)
{
    // **The test the whole curve editor stands on.** A picture of a curve is
    // worth having only if the sound follows it: the same settings go to the
    // panel and to the real block, a tone goes through the block, and what
    // comes out is read against what the panel drew.
    const std::vector<Setting> shape{
        number(u"threshold"_s, -24.0f),
        number(u"ratio"_s, 4.0f),
        number(u"knee"_s, 6.0f),
        number(u"makeup"_s, 3.0f),
        number(u"attack"_s, 1.0f),
        number(u"release"_s, 50.0f),
        number(u"expanderThreshold"_s, -60.0f),
        number(u"expanderRatio"_s, 2.0f),
        number(u"expanderRange"_s, 20.0f),
    };
    const std::unique_ptr<DynamicsPlot> plot = plotOf(mapOf(shape));

    for (const float inputDb : {-70.0f, -50.0f, -40.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f}) {
        blocks::Dynamics block;
        applyTo(block, shape);

        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal out = runner.run(stereoSine(32000, dsp::dbToGain(inputDb)), CycleShape::fixed(256));

        const double measured = peakDb(settled(out[0]));
        EXPECT_NEAR(measured, drawnOutputDb(*plot, inputDb), 0.3) << "at " << inputDb << " dBFS in";
    }
}

TEST(DynamicsPanelTest, TheMarkerMovesWithoutTheCurveBeingRebuilt)
{
    // The criterion: the marker moves every frame and the curve is
    // re-tessellated only when it changes shape (ADR-0009). A curve rebuilt
    // sixty times a second would be affordable and pointless.
    const std::unique_ptr<DynamicsPlot> plot = plotOf(mapOf({number(u"threshold"_s, -20.0f)}));
    const qulonglong drawn = plot->tessellations();

    for (int frame = 0; frame < 120; ++frame) {
        const double level = -60.0 + static_cast<double>(frame) / 2.0;
        const QPointF marker = plot->pointAt(level);
        EXPECT_GE(marker.x(), 0.0);
    }
    EXPECT_EQ(plot->tessellations(), drawn) << "the marker rebuilt the curve";

    // Nor does a setting that decides how the sound gets to the curve rather
    // than where the curve is: an attack being dragged is sixty edits a
    // second arriving here.
    QVariantMap timing = plot->params();
    timing.insert(u"attack"_s, 12.0);
    plot->setParams(timing);
    EXPECT_EQ(plot->tessellations(), drawn) << "a change that does not bend the curve rebuilt it";

    // And a threshold being dragged rebuilds it once for each value, which
    // is what the drawing is for.
    QVariantMap shape = plot->params();
    shape.insert(u"threshold"_s, -21.0);
    plot->setParams(shape);
    EXPECT_EQ(plot->tessellations(), drawn + 1);
}

TEST(DynamicsPanelTest, ASettingTheSessionDoesNotMentionIsTheBlocksDefault)
{
    // A session holds what somebody changed, not the whole block; the panel
    // draws the same curve the block would apply for everything it is not
    // told about.
    const std::unique_ptr<DynamicsPlot> plot = plotOf(QVariantMap{});
    const blocks::Dynamics block;

    for (const float inputDb : {-90.0f, -60.0f, -30.0f, -20.0f, -10.0f, 0.0f}) {
        EXPECT_NEAR(plot->outputDb(inputDb), dsp::outputDb(inputDb, block.settings()), 0.001)
            << "at " << inputDb << " dBFS in";
    }

    // And reading a second block's settings leaves nothing of the first's
    // behind: a panel moved from one tile to the next draws the new block.
    plot->setParams(mapOf({number(u"threshold"_s, -60.0f), number(u"ratio"_s, 8.0f)}));
    plot->setParams(QVariantMap{});
    for (const float inputDb : {-30.0f, -10.0f, 0.0f}) {
        EXPECT_NEAR(plot->outputDb(inputDb), dsp::outputDb(inputDb, block.settings()), 0.001)
            << "at " << inputDb << " dBFS in";
    }
}

TEST(DynamicsPanelTest, TheMarkerReadsTheBlocksOwnPublishedValues)
{
    // Which published value is which is the block's to say. The panel asking
    // rather than writing the positions down is what keeps the marker on the
    // input level when a value is one day added in front of it.
    const DynamicsPlot plot;

    EXPECT_EQ(plot.inputLevelIndex(), static_cast<int>(blocks::Dynamics::kInputLevelValue));
    EXPECT_EQ(plot.outputLevelIndex(), static_cast<int>(blocks::Dynamics::kOutputLevelValue));
    EXPECT_EQ(plot.gainReductionIndex(), static_cast<int>(blocks::Dynamics::kGainReductionValue));
}

TEST(DynamicsPanelTest, DecibelsAndThePlotAreEachOthersOpposite)
{
    // A handle is placed by one of these and dragged by the other, so a
    // handle that did not come back to where it was put would be a handle
    // that crept across the plot as it was used.
    const std::unique_ptr<DynamicsPlot> plot = plotOf(QVariantMap{});

    for (const double db : {-120.0, -80.0, -37.5, 0.0, 40.0}) {
        EXPECT_NEAR(plot->dbAtX(plot->xOfDb(db)), db, 1e-9);
        EXPECT_NEAR(plot->dbAtY(plot->yOfDb(db)), db, 1e-9);
    }

    // The quiet end is on the left and at the bottom, which is the way round
    // every transfer curve is drawn.
    EXPECT_LT(plot->xOfDb(-120.0), plot->xOfDb(0.0));
    EXPECT_GT(plot->yOfDb(-120.0), plot->yOfDb(0.0));
}

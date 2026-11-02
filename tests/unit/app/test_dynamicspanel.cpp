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

/// Which way \a c lies from the line through \a a and \a b, and how far.
[[nodiscard]] double turnOf(QPointF a, QPointF b, QPointF c)
{
    return (b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y());
}

/// Whether \a point, on the line through \a a and \a b, is between them.
[[nodiscard]] bool isBetween(QPointF point, QPointF a, QPointF b)
{
    return point.x() >= std::min(a.x(), b.x()) && point.x() <= std::max(a.x(), b.x())
        && point.y() >= std::min(a.y(), b.y()) && point.y() <= std::max(a.y(), b.y());
}

/// Whether two straight edges meet anywhere, ends included.
[[nodiscard]] bool edgesMeet(QPointF a, QPointF b, QPointF c, QPointF d)
{
    const double first = turnOf(a, b, c);
    const double second = turnOf(a, b, d);
    const double third = turnOf(c, d, a);
    const double fourth = turnOf(c, d, b);
    if (((first > 0.0) != (second > 0.0)) && ((third > 0.0) != (fourth > 0.0))) {
        return true;
    }
    return (first == 0.0 && isBetween(c, a, b)) || (second == 0.0 && isBetween(d, a, b))
        || (third == 0.0 && isBetween(a, c, d)) || (fourth == 0.0 && isBetween(b, c, d));
}

/// **A shape the renderer can fill:** no point twice, no edge going back along
/// the one before it, and no edge meeting any but its two neighbours.
[[nodiscard]] bool isSimple(const QList<QPointF> &outline)
{
    const qsizetype count = outline.size();
    const auto at = [&outline, count](qsizetype index) { return outline[index % count]; };
    for (qsizetype edge = 0; edge < count; ++edge) {
        const QPointF a = at(edge);
        const QPointF b = at(edge + 1);
        const QPointF c = at(edge + 2);
        if (a == b) {
            return false;
        }
        const QPointF out = b - a;
        const QPointF back = c - b;
        if (turnOf(a, b, c) == 0.0 && out.x() * back.x() + out.y() * back.y() < 0.0) {
            return false;
        }
        for (qsizetype other = edge + 2; other < count; ++other) {
            const bool neighbours = edge == 0 && other == count - 1;
            if (!neighbours && edgesMeet(a, b, at(other), at(other + 1))) {
                return false;
            }
        }
    }
    return true;
}

/// The area \a outline encloses.
[[nodiscard]] double areaOf(const QList<QPointF> &outline)
{
    double twice = 0.0;
    for (qsizetype index = 0; index < outline.size(); ++index) {
        const QPointF from = outline[index];
        const QPointF to = outline[(index + 1) % outline.size()];
        twice += from.x() * to.y() - to.x() * from.y();
    }
    return std::abs(twice) / 2.0;
}

/// The area between \a line and the bottom of a plot \a height tall, not
/// counting what is under that bottom.
[[nodiscard]] double areaUnder(const QList<QPointF> &line, double height)
{
    double area = 0.0;
    for (qsizetype index = 0; index + 1 < line.size(); ++index) {
        const double left = height - line[index].y();
        const double right = height - line[index + 1].y();
        const double width = line[index + 1].x() - line[index].x();
        if (left > 0.0 && right > 0.0) {
            area += width * (left + right) / 2.0;
        } else if (left > 0.0 || right > 0.0) {
            const double tall = std::max(left, right);
            area += width * tall / (tall - std::min(left, right)) * tall / 2.0;
        }
    }
    return area;
}

} // namespace

TEST(DynamicsPanelTest, TheDrawnLineIsTheBlocksOwnCurve)
{
    // The criterion: the polyline the Shape is handed is the curve the block
    // applies, within a tenth of a decibel at every decibel of the range.
    // **Half a decibel between samples** is finer than the bend, and the
    // corners are samples themselves, so the straight line the screen draws
    // between two of them is the curve.
    const std::vector<std::vector<Setting>> shapes{
        {number(u"threshold"_s, -24.0f), number(u"height"_s, 12.0f)},
        {number(u"threshold"_s, -6.0f), number(u"height"_s, 40.0f)},
        {number(u"threshold"_s, -40.0f), number(u"height"_s, 0.0f)},
        {number(u"expanderThreshold"_s, -50.0f),
         number(u"expanderRatio"_s, 3.0f),
         number(u"threshold"_s, -12.0f),
         number(u"height"_s, 6.0f)},
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
    constexpr float makeupDb = 3.0f;
    const std::vector<Setting> shape{
        number(u"threshold"_s, -24.0f),
        number(u"height"_s, 12.0f),
        number(u"makeup"_s, makeupDb),
        number(u"attack"_s, 1.0f),
        number(u"release"_s, 50.0f),
        number(u"expanderThreshold"_s, -60.0f),
        number(u"expanderRatio"_s, 2.0f),
    };
    const std::unique_ptr<DynamicsPlot> plot = plotOf(mapOf(shape));

    for (const float inputDb : {-70.0f, -50.0f, -40.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f}) {
        blocks::Dynamics block;
        applyTo(block, shape);

        BlockRunner runner(block, 2, 2);
        ASSERT_TRUE(runner.prepare().has_value());
        const MultiSignal out = runner.run(stereoSine(32000, dsp::dbToGain(inputDb)), CycleShape::fixed(256));

        // **The make-up gain is the sound's and not the graph's**, which is
        // the one place the two differ.
        const double measured = peakDb(settled(out[0]));
        EXPECT_NEAR(measured, drawnOutputDb(*plot, inputDb) + makeupDb, 0.3) << "at " << inputDb << " dBFS in";
    }
}

TEST(DynamicsPanelTest, TheMakeupMovesTheSoundAndNotTheLine)
{
    const std::unique_ptr<DynamicsPlot> flat = plotOf(mapOf({number(u"threshold"_s, -30.0f)}));
    const std::unique_ptr<DynamicsPlot> lifted =
        plotOf(mapOf({number(u"threshold"_s, -30.0f), number(u"makeup"_s, 20.0f)}));
    EXPECT_EQ(flat->curve(), lifted->curve());
}

TEST(DynamicsPanelTest, TheLineGoesThroughItsCornersExactly)
{
    // A steep line is less than one sample wide: without its corners among
    // the samples it would be drawn leaning, from somewhere else to somewhere
    // else.
    const std::unique_ptr<DynamicsPlot> plot =
        plotOf(mapOf({number(u"expanderThreshold"_s, -74.3f), number(u"expanderRatio"_s, 50.0f)}));
    const QList<QPointF> points = plot->curve();
    for (const QPointF wanted : {QPointF(plot->xOfDb(plot->cutoffDb()), plot->yOfDb(-120.0)),
                                 QPointF(plot->xOfDb(-74.3), plot->yOfDb(-74.3))}) {
        const bool found = std::ranges::any_of(points, [wanted](const QPointF &point) {
            return std::abs(point.x() - wanted.x()) < 1e-3 && std::abs(point.y() - wanted.y()) < 0.1;
        });
        EXPECT_TRUE(found) << "no point of the line at " << wanted.x() << ", " << wanted.y();
    }
}

TEST(DynamicsPanelTest, TheFourPointsStandWhereTheirLinesAre)
{
    const std::unique_ptr<DynamicsPlot> plot = plotOf(mapOf({number(u"expanderThreshold"_s, -60.0f),
                                                             number(u"expanderRatio"_s, 4.0f),
                                                             number(u"threshold"_s, -20.0f),
                                                             number(u"height"_s, 12.0f)}));
    EXPECT_NEAR(plot->cutoffDb(), -75.0, 1e-4);
    EXPECT_NEAR(plot->expanderStartDb(), -60.0, 1e-4);
    EXPECT_NEAR(plot->thresholdDb(), -20.0, 1e-4);
    EXPECT_NEAR(plot->endDb(), -8.0809, 1e-3);
    EXPECT_NEAR(plot->outputDb(plot->cutoffDb()), -120.0, 1e-3);

    // And what a dragged one means is the same arithmetic the other way.
    EXPECT_NEAR(plot->ratioForCutoff(-75.0), 4.0, 1e-3);
    EXPECT_NEAR(plot->heightForEnd(plot->endDb()), 12.0, 1e-2);
}

TEST(DynamicsPanelTest, ABandIsDrawnWithItsOwnLaw)
{
    // The Multiband Compressor's panel uses this plot for a band, and a band
    // still has a ratio and a knee, and its make-up gain on the line.
    DynamicsPlot plot;
    plot.setBand(true);
    plot.setPlotWidth(272.0);
    plot.setPlotHeight(272.0);
    plot.setParams(QVariantMap{{u"threshold"_s, -20.0}, {u"ratio"_s, 4.0}, {u"knee"_s, 0.0}, {u"makeup"_s, 3.0}});

    EXPECT_NEAR(plot.outputDb(-40.0), -37.0, 1e-3);
    EXPECT_NEAR(plot.outputDb(0.0), -12.0, 1e-3);
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
    plot->setParams(mapOf({number(u"threshold"_s, -60.0f), number(u"height"_s, 8.0f)}));
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

TEST(DynamicsPanelTest, TheTintIsOneCleanShapeAtEveryRatio)
{
    // **The renderer fills all of a shape or none of it.** An outline that
    // went back along itself lost the whole tint at about a third of the
    // ratios, scattered, so the slider is walked from end to end. On the
    // panel's range the floor is the bottom of the plot and the line lies
    // along it; on the tile's the line runs under the bottom. A start of
    // -119.5 puts a corner on a sample, which would be one point twice.
    struct Range {
        qreal lowest;
        qreal highest;
    };
    constexpr qreal kWidth = 206.0;
    constexpr qreal kHeight = 240.0;
    constexpr int kSteps = 60;
    for (const Range range : {Range{-120.0, 40.0}, Range{-60.0, 0.0}}) {
        for (const double start : {-119.5, -100.4, -60.0, -30.0}) {
            for (int step = 0; step <= kSteps; ++step) {
                const double ratio = std::pow(double{dsp::kDynamicsHighestRatio}, double(step) / kSteps);
                DynamicsPlot plot;
                plot.setLowestDb(range.lowest);
                plot.setHighestDb(range.highest);
                plot.setPlotWidth(kWidth);
                plot.setPlotHeight(kHeight);
                plot.setParams(
                    QVariantMap{{u"threshold"_s, -20.0}, {u"expanderThreshold"_s, start}, {u"expanderRatio"_s, ratio}});
                const QList<QPointF> outline = plot.filled();
                ASSERT_GE(outline.size(), 3) << "start " << start << " ratio " << ratio;
                ASSERT_TRUE(isSimple(outline)) << "from " << range.lowest << " start " << start << " ratio " << ratio;
                EXPECT_NEAR(areaOf(outline), areaUnder(plot.curve(), kHeight), 1e-6)
                    << "from " << range.lowest << " start " << start << " ratio " << ratio;
            }
        }
    }
}

TEST(DynamicsPanelTest, TheTintStandsOnTheBottomWhereverTheLineBegins)
{
    // With makeup a band's line starts above the bottom, and the tint comes
    // down to it at both ends.
    DynamicsPlot raised;
    raised.setBand(true);
    raised.setPlotWidth(272.0);
    raised.setPlotHeight(272.0);
    raised.setParams(QVariantMap{{u"threshold"_s, -20.0}, {u"ratio"_s, 4.0}, {u"knee"_s, 0.0}, {u"makeup"_s, 12.0}});
    const QList<QPointF> outline = raised.filled();
    ASSERT_GE(outline.size(), 3);
    EXPECT_TRUE(isSimple(outline));
    EXPECT_NEAR(areaOf(outline), areaUnder(raised.curve(), 272.0), 1e-6);
    EXPECT_EQ(outline.constFirst(), QPointF(0.0, 272.0));
    EXPECT_EQ(outline.constLast(), QPointF(272.0, 272.0));

    // A line wholly under the plot encloses nothing.
    DynamicsPlot under;
    under.setLowestDb(30.0);
    under.setHighestDb(40.0);
    under.setPlotWidth(272.0);
    under.setPlotHeight(272.0);
    under.setParams(QVariantMap{{u"threshold"_s, -20.0}});
    EXPECT_TRUE(under.filled().isEmpty());
}

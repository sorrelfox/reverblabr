// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/dynamicscurve.h"

#include <QList>
#include <QObject>
#include <QPointF>
#include <QQmlEngine>
#include <QVariantMap>

namespace rb::ui
{

/// The Dynamics block's transfer curve, placed on a plot.
///
/// **The curve is the engine's and the placing is the interface's.** What a
/// level comes out at is the DSP's one definition, reached through
/// core::DynamicsCurve; where that lands on the screen is this, and it is
/// here so that the drawn line, the handles on it, the live marker and the
/// scale beside it all read one mapping. Two mappings would be a handle that
/// does not sit on the curve it edits.
///
/// **The polyline is rebuilt when the shape changes, and not per frame.**
/// The marker moves sixty times a second and the curve does not
/// ([ADR-0009](../docs/adr/0009-canvas-rendering.md)), so the points are
/// worked out when the parameters or the size change and handed to the same
/// Shape until then. `tessellations` counts the rebuilds, which is how a
/// test sees that a moving marker costs none.
class DynamicsPlot : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The block's parameters, by id, as the session holds them.
    Q_PROPERTY(QVariantMap params READ params WRITE setParams NOTIFY paramsChanged FINAL)

    /// How big the plot is, in device-independent pixels.
    Q_PROPERTY(qreal plotWidth READ plotWidth WRITE setPlotWidth NOTIFY curveChanged FINAL)
    Q_PROPERTY(qreal plotHeight READ plotHeight WRITE setPlotHeight NOTIFY curveChanged FINAL)

    /// What the two axes run between, in decibels — the same range across
    /// and up, so that the unity diagonal is the diagonal. A panel shows the
    /// whole of it; a tile shows the part where a person's sound is.
    Q_PROPERTY(qreal lowestDb READ lowestDb WRITE setLowestDb NOTIFY curveChanged FINAL)
    Q_PROPERTY(qreal highestDb READ highestDb WRITE setHighestDb NOTIFY curveChanged FINAL)

    /// The curve itself, as points on the plot.
    Q_PROPERTY(QList<QPointF> curve READ curve NOTIFY curveChanged FINAL)
    /// The same line, closed along the bottom, for the tint under it.
    Q_PROPERTY(QList<QPointF> filled READ filled NOTIFY curveChanged FINAL)

    /// How many times the polyline has been worked out since this plot was
    /// made. **For a test**: a marker that moves without the curve being
    /// rebuilt is the thing ADR-0009 asks for, and it cannot be seen from
    /// the outside any other way.
    Q_PROPERTY(qulonglong tessellations READ tessellations NOTIFY curveChanged FINAL)

    /// Where the block's published levels sit in its telemetry.
    Q_PROPERTY(int inputLevelIndex READ inputLevelIndex CONSTANT FINAL)
    Q_PROPERTY(int outputLevelIndex READ outputLevelIndex CONSTANT FINAL)
    Q_PROPERTY(int gainReductionIndex READ gainReductionIndex CONSTANT FINAL)

public:
    /// How finely the curve is sampled. Half a decibel is finer than the
    /// knee bends over any width the block allows, so the straight line
    /// between two samples is within a hundredth of a decibel of the curve —
    /// far below what a plot two hundred pixels tall can show.
    static constexpr qreal kSampleStepDb = 0.5;

    explicit DynamicsPlot(QObject *parent = nullptr);

    [[nodiscard]] QVariantMap params() const;
    [[nodiscard]] qreal plotWidth() const;
    [[nodiscard]] qreal plotHeight() const;
    [[nodiscard]] qreal lowestDb() const;
    [[nodiscard]] qreal highestDb() const;
    [[nodiscard]] QList<QPointF> curve() const;
    [[nodiscard]] QList<QPointF> filled() const;
    [[nodiscard]] qulonglong tessellations() const;
    [[nodiscard]] int inputLevelIndex() const;
    [[nodiscard]] int outputLevelIndex() const;
    [[nodiscard]] int gainReductionIndex() const;

    void setParams(const QVariantMap &params);
    void setPlotWidth(qreal width);
    void setPlotHeight(qreal height);
    void setLowestDb(qreal db);
    void setHighestDb(qreal db);

    /// What a steady level comes out at, and how much is taken off on the
    /// way. The handles and the marker read these, so they cannot sit
    /// anywhere but on the line that is drawn.
    [[nodiscard]] Q_INVOKABLE qreal outputDb(qreal inputDb) const;
    [[nodiscard]] Q_INVOKABLE qreal gainReductionDb(qreal inputDb) const;

    /// Decibels to the plot and back.
    [[nodiscard]] Q_INVOKABLE qreal xOfDb(qreal inputDb) const;
    [[nodiscard]] Q_INVOKABLE qreal yOfDb(qreal outputDb) const;
    [[nodiscard]] Q_INVOKABLE qreal dbAtX(qreal x) const;
    [[nodiscard]] Q_INVOKABLE qreal dbAtY(qreal y) const;

    /// Where an input level lands on the curve: what a handle and the live
    /// marker are placed by.
    [[nodiscard]] Q_INVOKABLE QPointF pointAt(qreal inputDb) const;

Q_SIGNALS:
    void paramsChanged();
    void curveChanged();

private:
    /// Works the line out again and says so. **Done when something changes
    /// rather than when the line is next asked for**, so that the count of
    /// rebuilds is the count of changes and a test can read it at any
    /// moment.
    void rebuild();

    core::DynamicsCurve m_curve;
    QVariantMap m_params;
    qreal m_plotWidth = 0.0;
    qreal m_plotHeight = 0.0;
    qreal m_lowestDb = -120.0;
    qreal m_highestDb = 40.0;

    QList<QPointF> m_points;
    qulonglong m_tessellations = 0;
};

} // namespace rb::ui

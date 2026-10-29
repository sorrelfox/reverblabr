// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dynamicsplot.h"

#include <algorithm>
#include <cmath>

namespace rb::ui
{

DynamicsPlot::DynamicsPlot(QObject *parent)
    : QObject(parent)
{
}

QVariantMap DynamicsPlot::params() const
{
    return m_params;
}

qreal DynamicsPlot::plotWidth() const
{
    return m_plotWidth;
}

qreal DynamicsPlot::plotHeight() const
{
    return m_plotHeight;
}

qreal DynamicsPlot::lowestDb() const
{
    return m_lowestDb;
}

qreal DynamicsPlot::highestDb() const
{
    return m_highestDb;
}

QList<QPointF> DynamicsPlot::curve() const
{
    return m_points;
}

QList<QPointF> DynamicsPlot::filled() const
{
    if (m_points.isEmpty()) {
        return {};
    }
    // The same line, brought down to the bottom of the plot at both ends, so
    // that the tint under the curve is the area the curve encloses rather
    // than a second shape that could disagree with it.
    QList<QPointF> closed = m_points;
    closed.append(QPointF(m_points.constLast().x(), m_plotHeight));
    closed.append(QPointF(m_points.constFirst().x(), m_plotHeight));
    return closed;
}

qulonglong DynamicsPlot::tessellations() const
{
    return m_tessellations;
}

int DynamicsPlot::inputLevelIndex() const
{
    return core::DynamicsCurve::inputLevelIndex();
}

int DynamicsPlot::outputLevelIndex() const
{
    return core::DynamicsCurve::outputLevelIndex();
}

int DynamicsPlot::gainReductionIndex() const
{
    return core::DynamicsCurve::gainReductionIndex();
}

void DynamicsPlot::setParams(const QVariantMap &params)
{
    if (m_params == params) {
        return;
    }
    m_params = params;
    Q_EMIT paramsChanged();
    // **Only a parameter that bends the curve costs a rebuild.** An attack,
    // a hold or a detector changes how the sound reaches the curve and not
    // where the curve is, and a drag of one of those is sixty edits a second
    // arriving here.
    if (m_curve.setParams(m_params)) {
        rebuild();
    }
}

void DynamicsPlot::setPlotWidth(qreal width)
{
    if (qFuzzyCompare(m_plotWidth, width)) {
        return;
    }
    m_plotWidth = width;
    rebuild();
}

void DynamicsPlot::setPlotHeight(qreal height)
{
    if (qFuzzyCompare(m_plotHeight, height)) {
        return;
    }
    m_plotHeight = height;
    rebuild();
}

void DynamicsPlot::setLowestDb(qreal db)
{
    if (qFuzzyCompare(m_lowestDb, db)) {
        return;
    }
    m_lowestDb = db;
    rebuild();
}

void DynamicsPlot::setHighestDb(qreal db)
{
    if (qFuzzyCompare(m_highestDb, db)) {
        return;
    }
    m_highestDb = db;
    rebuild();
}

qreal DynamicsPlot::outputDb(qreal inputDb) const
{
    return m_curve.outputDb(inputDb);
}

qreal DynamicsPlot::gainReductionDb(qreal inputDb) const
{
    return m_curve.gainReductionDb(inputDb);
}

qreal DynamicsPlot::xOfDb(qreal inputDb) const
{
    const qreal span = m_highestDb - m_lowestDb;
    if (span <= 0.0) {
        return 0.0;
    }
    return (inputDb - m_lowestDb) / span * m_plotWidth;
}

qreal DynamicsPlot::yOfDb(qreal outputDb) const
{
    const qreal span = m_highestDb - m_lowestDb;
    if (span <= 0.0) {
        return 0.0;
    }
    // Upwards is louder, and a plot's y grows downwards.
    return m_plotHeight - (outputDb - m_lowestDb) / span * m_plotHeight;
}

qreal DynamicsPlot::dbAtX(qreal x) const
{
    if (m_plotWidth <= 0.0) {
        return m_lowestDb;
    }
    return m_lowestDb + x / m_plotWidth * (m_highestDb - m_lowestDb);
}

qreal DynamicsPlot::dbAtY(qreal y) const
{
    if (m_plotHeight <= 0.0) {
        return m_lowestDb;
    }
    return m_lowestDb + (m_plotHeight - y) / m_plotHeight * (m_highestDb - m_lowestDb);
}

QPointF DynamicsPlot::pointAt(qreal inputDb) const
{
    return QPointF(xOfDb(inputDb), yOfDb(outputDb(inputDb)));
}

void DynamicsPlot::rebuild()
{
    m_points.clear();
    ++m_tessellations;

    const qreal span = m_highestDb - m_lowestDb;
    if (m_plotWidth > 0.0 && m_plotHeight > 0.0 && span > 0.0) {
        const auto steps = static_cast<int>(std::ceil(span / kSampleStepDb));
        m_points.reserve(steps + 1);
        for (int step = 0; step <= steps; ++step) {
            // The last sample lands on the top of the range exactly,
            // whatever the step leaves over.
            const qreal inputDb = std::min(m_lowestDb + static_cast<qreal>(step) * kSampleStepDb, m_highestDb);
            m_points.append(pointAt(inputDb));
        }
    }
    Q_EMIT curveChanged();
}

} // namespace rb::ui

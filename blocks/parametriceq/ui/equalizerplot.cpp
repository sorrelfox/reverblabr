// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "equalizerplot.h"

#include "core/telemetry.h"

#include <algorithm>
#include <cmath>

namespace rb::ui
{

EqualizerPlot::EqualizerPlot(QObject *parent)
    : QObject(parent)
{
}

QVariantMap EqualizerPlot::params() const
{
    return m_params;
}

qreal EqualizerPlot::plotWidth() const
{
    return m_plotWidth;
}

qreal EqualizerPlot::plotHeight() const
{
    return m_plotHeight;
}

qreal EqualizerPlot::rangeDb() const
{
    return m_rangeDb;
}

int EqualizerPlot::selectedBand() const
{
    return m_selectedBand;
}

QList<QPointF> EqualizerPlot::curve() const
{
    return m_points;
}

QList<QPointF> EqualizerPlot::bandCurve() const
{
    return m_bandPoints;
}

qulonglong EqualizerPlot::tessellations() const
{
    return m_tessellations;
}

QVariantList EqualizerPlot::spectrum() const
{
    return m_spectrum;
}

QList<QPointF> EqualizerPlot::spectrumArea() const
{
    return m_spectrumArea;
}

void EqualizerPlot::setSpectrum(const QVariantList &bandsDb)
{
    if (bandsDb == m_spectrum) {
        return;
    }
    m_spectrum = bandsDb;
    placeSpectrum();
}

qreal EqualizerPlot::yOfSpectrumDb(qreal db) const
{
    const qreal clamped = std::clamp(db, kSpectrumFloorDb, 0.0);
    return clamped / kSpectrumFloorDb * m_plotHeight;
}

void EqualizerPlot::placeSpectrum()
{
    // **Not the curve's rebuild**: a spectrum is new sixty times a second
    // and the curve is not, so the curve's polyline is left alone and its
    // count of rebuilds with it.
    m_spectrumArea.clear();
    if (!m_spectrum.isEmpty() && m_plotWidth > 0.0 && m_plotHeight > 0.0) {
        const auto bands = std::min<qsizetype>(m_spectrum.size(), static_cast<qsizetype>(core::kSpectrumBands));
        m_spectrumArea.append(QPointF(0.0, m_plotHeight));
        for (qsizetype band = 0; band < bands; ++band) {
            const qreal hz = core::spectrumBandCentreHz(static_cast<std::size_t>(band));
            m_spectrumArea.append(QPointF(xOfHz(hz), yOfSpectrumDb(m_spectrum.at(band).toDouble())));
        }
        m_spectrumArea.append(QPointF(m_plotWidth, m_plotHeight));
    }
    Q_EMIT spectrumChanged();
}

qreal EqualizerPlot::lowestHz() const
{
    return kLowestHz;
}

qreal EqualizerPlot::highestHz() const
{
    return kHighestHz;
}

int EqualizerPlot::bandCount() const
{
    return core::EqualizerCurve::bandCount();
}

void EqualizerPlot::setParams(const QVariantMap &params)
{
    if (m_params == params) {
        return;
    }
    m_params = params;
    Q_EMIT paramsChanged();
    // Only a change that moves the response costs a rebuild: a band's
    // parameters written again with the same values move nothing.
    if (m_curve.setParams(m_params)) {
        rebuild();
    }
}

void EqualizerPlot::setPlotWidth(qreal width)
{
    if (qFuzzyCompare(m_plotWidth, width)) {
        return;
    }
    m_plotWidth = width;
    rebuild();
    placeSpectrum();
}

void EqualizerPlot::setPlotHeight(qreal height)
{
    if (qFuzzyCompare(m_plotHeight, height)) {
        return;
    }
    m_plotHeight = height;
    rebuild();
    placeSpectrum();
}

void EqualizerPlot::setRangeDb(qreal db)
{
    if (qFuzzyCompare(m_rangeDb, db) || db <= 0.0) {
        return;
    }
    m_rangeDb = db;
    rebuild();
}

void EqualizerPlot::setSelectedBand(int band)
{
    if (m_selectedBand == band) {
        return;
    }
    m_selectedBand = band;
    rebuild();
}

qreal EqualizerPlot::magnitudeDb(qreal hz) const
{
    return m_curve.magnitudeDb(hz);
}

qreal EqualizerPlot::bandDb(int band, qreal hz) const
{
    return m_curve.bandDb(band, hz);
}

QString EqualizerPlot::paramId(int band, const QString &which) const
{
    return core::EqualizerCurve::paramId(band, which);
}

qreal EqualizerPlot::xOfHz(qreal hz) const
{
    const qreal clamped = std::clamp(hz, kLowestHz, kHighestHz);
    return std::log(clamped / kLowestHz) / std::log(kHighestHz / kLowestHz) * m_plotWidth;
}

qreal EqualizerPlot::hzAtX(qreal x) const
{
    if (m_plotWidth <= 0.0) {
        return kLowestHz;
    }
    const qreal along = std::clamp(x / m_plotWidth, 0.0, 1.0);
    return kLowestHz * std::pow(kHighestHz / kLowestHz, along);
}

qreal EqualizerPlot::yOfDb(qreal db) const
{
    // 0 dB across the middle, +range at the top and -range at the bottom.
    const qreal clamped = std::clamp(db, -m_rangeDb, m_rangeDb);
    return (m_rangeDb - clamped) / (2.0 * m_rangeDb) * m_plotHeight;
}

qreal EqualizerPlot::dbAtY(qreal y) const
{
    if (m_plotHeight <= 0.0) {
        return 0.0;
    }
    const qreal along = std::clamp(y / m_plotHeight, 0.0, 1.0);
    return m_rangeDb - along * 2.0 * m_rangeDb;
}

void EqualizerPlot::rebuild()
{
    ++m_tessellations;
    m_points.clear();
    m_bandPoints.clear();
    if (m_plotWidth > 0.0 && m_plotHeight > 0.0) {
        const bool withBand = m_selectedBand >= 0 && m_selectedBand < bandCount();
        for (qreal x = 0.0;; x += kSampleStepPixels) {
            const qreal at = std::min(x, m_plotWidth);
            const qreal hz = hzAtX(at);
            // A notch goes to nothing at its centre; the line goes to the
            // bottom of the plot, which is as far as the plot can say.
            m_points.append(QPointF(at, yOfDb(m_curve.magnitudeDb(hz))));
            if (withBand) {
                m_bandPoints.append(QPointF(at, yOfDb(m_curve.bandDb(m_selectedBand, hz))));
            }
            if (at >= m_plotWidth) {
                break;
            }
        }
    }
    Q_EMIT curveChanged();
}

} // namespace rb::ui

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/equalizercurve.h"

#include <QList>
#include <QObject>
#include <QPointF>
#include <QQmlEngine>
#include <QVariantMap>

namespace rb::ui
{

/// The Parametric EQ's response, placed on a plot: frequency across on
/// a logarithmic scale, gain up in decibels.
///
/// **The curve is the engine's and the placing is the interface's**, as for
/// DynamicsPlot: the response comes from core::EqualizerCurve, and this is
/// the one mapping the drawn line, the band handles and the grid all read,
/// so a handle cannot sit anywhere the line is not.
///
/// **The polyline is rebuilt when the bands or the size change**, not per
/// frame; `tessellations` counts the rebuilds for a test.
class EqualizerPlot : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The block's parameters, by id, as the session holds them.
    Q_PROPERTY(QVariantMap params READ params WRITE setParams NOTIFY paramsChanged FINAL)
    Q_PROPERTY(qreal plotWidth READ plotWidth WRITE setPlotWidth NOTIFY curveChanged FINAL)
    Q_PROPERTY(qreal plotHeight READ plotHeight WRITE setPlotHeight NOTIFY curveChanged FINAL)
    /// How far up and down the plot reaches, in dB either side of 0.
    Q_PROPERTY(qreal rangeDb READ rangeDb WRITE setRangeDb NOTIFY curveChanged FINAL)
    /// The band whose own part of the response is drawn as well, or -1.
    Q_PROPERTY(int selectedBand READ selectedBand WRITE setSelectedBand NOTIFY curveChanged FINAL)

    /// The whole response, as points on the plot.
    Q_PROPERTY(QList<QPointF> curve READ curve NOTIFY curveChanged FINAL)
    /// The selected band's part of it alone, or nothing.
    Q_PROPERTY(QList<QPointF> bandCurve READ bandCurve NOTIFY curveChanged FINAL)
    Q_PROPERTY(qulonglong tessellations READ tessellations NOTIFY curveChanged FINAL)

    /// **The live spectrum behind the curve**: one level in dB a
    /// band, low to high, as Meters.spectrum() gives it — or an empty list,
    /// and nothing is drawn. Placed on its own scale, kSpectrumFloorDb at the
    /// bottom to 0 dB at the top, since what a signal's level is and what the
    /// curve does to it are different questions.
    Q_PROPERTY(QVariantList spectrum READ spectrum WRITE setSpectrum NOTIFY spectrumChanged FINAL)
    /// The same, as a shape to fill: along the band centres and back along
    /// the bottom of the plot. Empty with no spectrum.
    Q_PROPERTY(QList<QPointF> spectrumArea READ spectrumArea NOTIFY spectrumChanged FINAL)

    /// The ends of the frequency axis, and how many bands there are.
    Q_PROPERTY(qreal lowestHz READ lowestHz CONSTANT FINAL)
    Q_PROPERTY(qreal highestHz READ highestHz CONSTANT FINAL)
    Q_PROPERTY(int bandCount READ bandCount CONSTANT FINAL)

public:
    static constexpr qreal kLowestHz = 20.0;
    static constexpr qreal kHighestHz = 20000.0;
    /// How finely the curve is sampled: a pixel apart, which is as fine as
    /// the screen can show, and the narrowest notch the block allows is
    /// wider than that at any plot a panel draws.
    static constexpr qreal kSampleStepPixels = 1.0;
    /// The bottom of the spectrum's scale: quieter than this is the floor.
    static constexpr qreal kSpectrumFloorDb = -90.0;

    explicit EqualizerPlot(QObject *parent = nullptr);

    [[nodiscard]] QVariantMap params() const;
    [[nodiscard]] qreal plotWidth() const;
    [[nodiscard]] qreal plotHeight() const;
    [[nodiscard]] qreal rangeDb() const;
    [[nodiscard]] int selectedBand() const;
    [[nodiscard]] QList<QPointF> curve() const;
    [[nodiscard]] QList<QPointF> bandCurve() const;
    [[nodiscard]] qulonglong tessellations() const;
    [[nodiscard]] QVariantList spectrum() const;
    [[nodiscard]] QList<QPointF> spectrumArea() const;
    void setSpectrum(const QVariantList &bandsDb);
    [[nodiscard]] qreal lowestHz() const;
    [[nodiscard]] qreal highestHz() const;
    [[nodiscard]] int bandCount() const;

    void setParams(const QVariantMap &params);
    void setPlotWidth(qreal width);
    void setPlotHeight(qreal height);
    void setRangeDb(qreal db);
    void setSelectedBand(int band);

    /// The response at a frequency, in dB, and one band's part of it.
    [[nodiscard]] Q_INVOKABLE qreal magnitudeDb(qreal hz) const;
    [[nodiscard]] Q_INVOKABLE qreal bandDb(int band, qreal hz) const;

    /// The id of one of a band's parameters: "band3Gain".
    [[nodiscard]] Q_INVOKABLE QString paramId(int band, const QString &which) const;

    /// Hertz and decibels to the plot and back. A frequency past either
    /// end, and a gain past the range, land on the edge.
    [[nodiscard]] Q_INVOKABLE qreal xOfHz(qreal hz) const;
    [[nodiscard]] Q_INVOKABLE qreal hzAtX(qreal x) const;
    [[nodiscard]] Q_INVOKABLE qreal yOfDb(qreal db) const;
    [[nodiscard]] Q_INVOKABLE qreal dbAtY(qreal y) const;
    /// A spectrum level, in dB, to the plot's height.
    [[nodiscard]] Q_INVOKABLE qreal yOfSpectrumDb(qreal db) const;

Q_SIGNALS:
    void paramsChanged();
    void curveChanged();
    void spectrumChanged();

private:
    void rebuild();
    void placeSpectrum();

    core::EqualizerCurve m_curve;
    QVariantMap m_params;
    qreal m_plotWidth = 0.0;
    qreal m_plotHeight = 0.0;
    qreal m_rangeDb = 24.0;
    int m_selectedBand = -1;
    QList<QPointF> m_points;
    QList<QPointF> m_bandPoints;
    QVariantList m_spectrum;
    QList<QPointF> m_spectrumArea;
    qulonglong m_tessellations = 0;
};

} // namespace rb::ui

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "scopesurface.h"
#include "spectrumview.h"

#include <QColor>
#include <QString>
#include <QVariantList>

#include <vector>

namespace rb::ui
{

/// **A block's spectrum, in bars, drawn on the scene graph**, on its tile and
/// on its card. Every setting is the surface's: the block feeds the same
/// slot whatever it shows.
///
/// **It draws no more bars than it has room for.** A bar under kLeastPitch
/// pixels is a smear, so a tile shows fewer than a card, and they are real
/// bars: each is worked out at its own width and not by leaving others out, so
/// a sine has its full height on either.
class SpectrumItem : public ScopeSurface
{
    Q_OBJECT
    QML_ELEMENT

    /// The block's settings, in the units of the sum and not the session's
    /// ids, which are QML's to turn into these.
    Q_PROPERTY(int bars READ bars WRITE setBars NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString frequencyScale READ frequencyScale WRITE setFrequencyScale NOTIFY settingsChanged FINAL)
    Q_PROPERTY(int frameSize READ frameSize WRITE setFrameSize NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal floorDb READ floorDb WRITE setFloorDb NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal tiltDb READ tiltDb WRITE setTiltDb NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool peakMarks READ peakMarks WRITE setPeakMarks NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString channels READ channels WRITE setChannels NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool grid READ grid WRITE setGrid NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString barStyle READ barStyle WRITE setBarStyle NOTIFY settingsChanged FINAL)

    Q_PROPERTY(QColor barColor READ barColor WRITE setBarColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor peakColor READ peakColor WRITE setPeakColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged FINAL)

    /// How many bars are drawn at this width.
    Q_PROPERTY(int barsShown READ barsShown NOTIFY barsShownChanged FINAL)
    /// How much sound a frame covers, and how far apart two tones must be for
    /// it to tell them from one another: nought while the rate is not known.
    Q_PROPERTY(qreal frameMs READ frameMs NOTIFY readingsChanged FINAL)
    Q_PROPERTY(qreal resolutionHz READ resolutionHz NOTIFY readingsChanged FINAL)
    /// What the grid marks: { hz, position, labelled } across, and { db,
    /// position } up, positions from nought to one.
    Q_PROPERTY(QVariantList frequencyMarks READ frequencyMarks NOTIFY marksChanged FINAL)
    Q_PROPERTY(QVariantList levelMarks READ levelMarks NOTIFY marksChanged FINAL)

public:
    /// A bar's pitch at the narrowest, in pixels.
    static constexpr int kLeastPitch = 3;
    /// A segment's height with the gap above it, and a peak mark's height.
    static constexpr float kSegmentPitch = 4.0f;
    static constexpr float kPeakThickness = 2.0f;

    explicit SpectrumItem(QQuickItem *parent = nullptr);
    ~SpectrumItem() override;

    [[nodiscard]] int bars() const;
    void setBars(int bars);
    [[nodiscard]] QString frequencyScale() const;
    void setFrequencyScale(const QString &scale);
    [[nodiscard]] int frameSize() const;
    void setFrameSize(int frames);
    [[nodiscard]] qreal floorDb() const;
    void setFloorDb(qreal db);
    [[nodiscard]] qreal tiltDb() const;
    void setTiltDb(qreal db);
    [[nodiscard]] bool peakMarks() const;
    void setPeakMarks(bool on);
    [[nodiscard]] QString channels() const;
    void setChannels(const QString &channels);
    [[nodiscard]] bool grid() const;
    void setGrid(bool on);
    [[nodiscard]] QString barStyle() const;
    void setBarStyle(const QString &style);

    [[nodiscard]] QColor barColor() const;
    void setBarColor(const QColor &color);
    [[nodiscard]] QColor peakColor() const;
    void setPeakColor(const QColor &color);
    [[nodiscard]] QColor gridColor() const;
    void setGridColor(const QColor &color);

    [[nodiscard]] int barsShown() const;
    [[nodiscard]] qreal frameMs() const;
    [[nodiscard]] qreal resolutionHz() const;
    [[nodiscard]] QVariantList frequencyMarks() const;
    [[nodiscard]] QVariantList levelMarks() const;

    /// Each bar's level now, and each peak mark's, in dB, for a test.
    [[nodiscard]] Q_INVOKABLE QVariantList levels() const;
    [[nodiscard]] Q_INVOKABLE QVariantList peakLevels() const;
    /// Which bar shows \a hz.
    [[nodiscard]] Q_INVOKABLE int barOf(qreal hz) const;

Q_SIGNALS:
    void barsShownChanged();
    void marksChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void startAfresh() override;
    bool workOutPicture() override;
    void sayOwnReadings() override;

private:
    /// A line of the grid: a frequency or a level, where it goes, from nought
    /// to one, and whether it is named.
    struct Mark {
        double value = 0.0;
        double position = 0.0;
        bool labelled = false;
    };

    [[nodiscard]] std::vector<Mark> marksAcross() const;
    [[nodiscard]] std::vector<Mark> marksUp() const;
    [[nodiscard]] double sampleRate() const;
    void sayBarsShown();

    SpectrumSettings m_spectrum;
    SpectrumView m_view;
    int m_bars = 64;
    double m_floorDb = -80.0;
    int m_saidBarsShown = 0;
    QString m_scale{QStringLiteral("logarithmic")};
    QString m_channels{QStringLiteral("mix")};
    QString m_style{QStringLiteral("solid")};
    bool m_peakMarks = true;
    bool m_grid = true;

    QColor m_barColor{QStringLiteral("#3daee9")};
    QColor m_peakColor{QStringLiteral("#f5a623")};
    QColor m_gridColor{QStringLiteral("#31363b")};

    double m_saidRate = 0.0;
};

} // namespace rb::ui

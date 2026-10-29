// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QColor>
#include <QList>
#include <QQuickItem>

namespace rb::ui
{

/// **A recording's outline**, drawn with the scene graph: a bar for
/// each few pixels, as high as the loudest sample in its share of the file,
/// centred on the middle line as the design draws it. The part already
/// listened to is in one colour and the rest in another.
///
/// The peaks come from the recordings index (512 of them for the whole
/// file), and as many bars are drawn as the width has room for, each the
/// loudest of the peaks under it — so a short window and a wide one both
/// show every loud moment and never average one away.
class WaveformItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    /// Amplitudes from 0 to 1, from the file's start to its end.
    Q_PROPERTY(QList<qreal> peaks READ peaks WRITE setPeaks NOTIFY peaksChanged)
    /// How far through the file has been listened to, from 0 to 1.
    Q_PROPERTY(qreal progress READ progress WRITE setProgress NOTIFY progressChanged)
    Q_PROPERTY(QColor playedColor READ playedColor WRITE setPlayedColor NOTIFY coloursChanged)
    Q_PROPERTY(QColor restColor READ restColor WRITE setRestColor NOTIFY coloursChanged)
    /// A bar's width and the gap after it, in pixels: the design's 3 and 1.
    Q_PROPERTY(qreal barWidth READ barWidth WRITE setBarWidth NOTIFY barsChanged)
    Q_PROPERTY(qreal barGap READ barGap WRITE setBarGap NOTIFY barsChanged)
    /// How many bars are drawn now, for a test.
    Q_PROPERTY(int barCount READ barCount NOTIFY barsChanged)

public:
    explicit WaveformItem(QQuickItem *parent = nullptr);

    [[nodiscard]] QList<qreal> peaks() const;
    void setPeaks(const QList<qreal> &peaks);
    [[nodiscard]] qreal progress() const;
    void setProgress(qreal progress);
    [[nodiscard]] QColor playedColor() const;
    void setPlayedColor(const QColor &colour);
    [[nodiscard]] QColor restColor() const;
    void setRestColor(const QColor &colour);
    [[nodiscard]] qreal barWidth() const;
    void setBarWidth(qreal width);
    [[nodiscard]] qreal barGap() const;
    void setBarGap(qreal gap);
    [[nodiscard]] int barCount() const;

    /// The bars \a peaks come to at \a count bars: each the loudest of the
    /// peaks that fall under it, or, with fewer peaks than bars, the peak
    /// it falls on.
    [[nodiscard]] static QList<qreal> barsOf(const QList<qreal> &peaks, int count);

    /// The bar heights now, for a test that asks what is drawn.
    [[nodiscard]] Q_INVOKABLE QList<qreal> bars() const;

    /// Where \a x falls in the file, from 0 to 1: what a click seeks to.
    [[nodiscard]] Q_INVOKABLE qreal fractionAt(qreal x) const;

Q_SIGNALS:
    void peaksChanged();
    void progressChanged();
    void coloursChanged();
    void barsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    QList<qreal> m_peaks;
    qreal m_progress = 0.0;
    QColor m_playedColor{Qt::white};
    QColor m_restColor{Qt::gray};
    qreal m_barWidth = 3.0;
    qreal m_barGap = 1.0;
};

} // namespace rb::ui

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "waveformitem.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGNode>

#include <algorithm>
#include <cmath>

namespace rb::ui
{

namespace
{

/// One flat-coloured batch of bars, two triangles each: the scene graph
/// batches by material, so the whole outline is two nodes.
class BarsNode : public QSGGeometryNode
{
public:
    BarsNode()
        : m_geometry(QSGGeometry::defaultAttributes_Point2D(), 0)
    {
        m_geometry.setDrawingMode(QSGGeometry::DrawTriangles);
        setGeometry(&m_geometry);
        setMaterial(&m_material);
    }

    void setColour(const QColor &colour)
    {
        if (m_material.color() != colour) {
            m_material.setColor(colour);
            markDirty(QSGNode::DirtyMaterial);
        }
    }

    void setBars(const QList<QRectF> &bars)
    {
        const int vertices = static_cast<int>(bars.size()) * 6;
        if (m_geometry.vertexCount() != vertices) {
            m_geometry.allocate(vertices);
        }
        QSGGeometry::Point2D *point = m_geometry.vertexDataAsPoint2D();
        for (const QRectF &bar : bars) {
            const auto x1 = static_cast<float>(bar.left());
            const auto y1 = static_cast<float>(bar.top());
            const auto x2 = static_cast<float>(bar.right());
            const auto y2 = static_cast<float>(bar.bottom());
            point[0].set(x1, y1);
            point[1].set(x2, y1);
            point[2].set(x1, y2);
            point[3].set(x2, y1);
            point[4].set(x2, y2);
            point[5].set(x1, y2);
            point += 6;
        }
        markDirty(QSGNode::DirtyGeometry);
    }

private:
    QSGGeometry m_geometry;
    QSGFlatColorMaterial m_material;
};

} // namespace

WaveformItem::WaveformItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QList<qreal> WaveformItem::peaks() const
{
    return m_peaks;
}

void WaveformItem::setPeaks(const QList<qreal> &peaks)
{
    if (peaks == m_peaks) {
        return;
    }
    m_peaks = peaks;
    Q_EMIT peaksChanged();
    Q_EMIT barsChanged();
    update();
}

qreal WaveformItem::progress() const
{
    return m_progress;
}

void WaveformItem::setProgress(qreal progress)
{
    progress = std::clamp(progress, 0.0, 1.0);
    if (qFuzzyCompare(progress + 1.0, m_progress + 1.0)) {
        return;
    }
    m_progress = progress;
    Q_EMIT progressChanged();
    update();
}

QColor WaveformItem::playedColor() const
{
    return m_playedColor;
}

void WaveformItem::setPlayedColor(const QColor &colour)
{
    if (colour != m_playedColor) {
        m_playedColor = colour;
        Q_EMIT coloursChanged();
        update();
    }
}

QColor WaveformItem::restColor() const
{
    return m_restColor;
}

void WaveformItem::setRestColor(const QColor &colour)
{
    if (colour != m_restColor) {
        m_restColor = colour;
        Q_EMIT coloursChanged();
        update();
    }
}

qreal WaveformItem::barWidth() const
{
    return m_barWidth;
}

void WaveformItem::setBarWidth(qreal width)
{
    width = std::max(1.0, width);
    if (!qFuzzyCompare(width, m_barWidth)) {
        m_barWidth = width;
        Q_EMIT barsChanged();
        update();
    }
}

qreal WaveformItem::barGap() const
{
    return m_barGap;
}

void WaveformItem::setBarGap(qreal gap)
{
    gap = std::max(0.0, gap);
    if (!qFuzzyCompare(gap + 1.0, m_barGap + 1.0)) {
        m_barGap = gap;
        Q_EMIT barsChanged();
        update();
    }
}

int WaveformItem::barCount() const
{
    if (width() <= 0.0 || m_peaks.isEmpty()) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::floor((width() + m_barGap) / (m_barWidth + m_barGap))));
}

QList<qreal> WaveformItem::barsOf(const QList<qreal> &peaks, int count)
{
    QList<qreal> bars;
    if (peaks.isEmpty() || count <= 0) {
        return bars;
    }
    bars.reserve(count);
    const auto size = peaks.size();
    for (int bar = 0; bar < count; ++bar) {
        const qsizetype from = bar * size / count;
        const qsizetype to = std::max(from + 1, (bar + 1) * size / count);
        qreal loudest = 0.0;
        for (qsizetype index = from; index < to && index < size; ++index) {
            loudest = std::max(loudest, peaks.at(index));
        }
        bars.append(std::clamp(loudest, 0.0, 1.0));
    }
    return bars;
}

QList<qreal> WaveformItem::bars() const
{
    return barsOf(m_peaks, barCount());
}

qreal WaveformItem::fractionAt(qreal x) const
{
    return width() > 0.0 ? std::clamp(x / width(), 0.0, 1.0) : 0.0;
}

void WaveformItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        Q_EMIT barsChanged();
        update();
    }
}

QSGNode *WaveformItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    const QList<qreal> heights = bars();
    if (heights.isEmpty() || height() <= 0.0) {
        delete node;
        return nullptr;
    }
    if (node == nullptr) {
        node = new QSGNode;
        node->appendChildNode(new BarsNode);
        node->appendChildNode(new BarsNode);
    }
    auto *played = static_cast<BarsNode *>(node->childAtIndex(0));
    auto *rest = static_cast<BarsNode *>(node->childAtIndex(1));
    played->setColour(m_playedColor);
    rest->setColour(m_restColor);

    QList<QRectF> before;
    QList<QRectF> after;
    const auto count = static_cast<qreal>(heights.size());
    const qreal middle = height() / 2.0;
    for (qsizetype index = 0; index < heights.size(); ++index) {
        const qreal x = static_cast<qreal>(index) * (m_barWidth + m_barGap);
        // A floor of a pixel, so silence is still a line along the middle.
        const qreal bar = std::max(1.0, heights.at(index) * height());
        const QRectF rectangle(x, middle - bar / 2.0, m_barWidth, bar);
        if ((static_cast<qreal>(index) + 0.5) / count <= m_progress) {
            before.append(rectangle);
        } else {
            after.append(rectangle);
        }
    }
    played->setBars(before);
    rest->setBars(after);
    return node;
}

} // namespace rb::ui

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "meteritem.h"

#include "meterscale.h"

#include <QSGFlatColorMaterial>
#include <QSGGeometryNode>
#include <QSGNode>

#include <algorithm>

namespace rb::ui
{

namespace
{

constexpr int kVerticesPerRectangle = 6; // two triangles

void writeRectangle(QSGGeometry::Point2D *vertices, const QRectF &rectangle)
{
    const auto x1 = static_cast<float>(rectangle.left());
    const auto y1 = static_cast<float>(rectangle.top());
    const auto x2 = static_cast<float>(rectangle.right());
    const auto y2 = static_cast<float>(rectangle.bottom());
    vertices[0].set(x1, y1);
    vertices[1].set(x2, y1);
    vertices[2].set(x1, y2);
    vertices[3].set(x2, y1);
    vertices[4].set(x2, y2);
    vertices[5].set(x1, y2);
}

/// One flat-coloured batch of rectangles. **One node per colour**, not one
/// per rectangle: the scene graph batches by material, and fifty meters of
/// four colours each is two hundred nodes rather than a thousand.
class ColourNode : public QSGGeometryNode
{
public:
    explicit ColourNode(const QColor &colour)
        : m_geometry(QSGGeometry::defaultAttributes_Point2D(), 0)
    {
        m_geometry.setDrawingMode(QSGGeometry::DrawTriangles);
        setGeometry(&m_geometry);
        m_material.setColor(colour);
        setMaterial(&m_material);
    }

    void setColour(const QColor &colour)
    {
        if (m_material.color() != colour) {
            m_material.setColor(colour);
            markDirty(QSGNode::DirtyMaterial);
        }
    }

    /// Replaces every rectangle in one go.
    void setRectangles(const QList<QRectF> &rectangles)
    {
        const int vertices = static_cast<int>(rectangles.size()) * kVerticesPerRectangle;
        if (m_geometry.vertexCount() != vertices) {
            m_geometry.allocate(vertices);
        }
        QSGGeometry::Point2D *point = m_geometry.vertexDataAsPoint2D();
        for (const QRectF &rectangle : rectangles) {
            writeRectangle(point, rectangle);
            point += kVerticesPerRectangle;
        }
        markDirty(QSGNode::DirtyGeometry);
    }

private:
    QSGGeometry m_geometry;
    QSGFlatColorMaterial m_material;
};

/// The order the batches are drawn in, back to front: a well, a peak bar, an
/// RMS bar over it and a hold line for each channel, and one clip light
/// across the top of the whole meter.
///
/// **The peak before the RMS**. The peak bar is always at least as
/// tall as the RMS bar, so it has to be drawn first for the RMS to show over
/// it and leave only the tip above it in the peak's colour. The other way
/// round, the peak covered the RMS entirely and every meter in the
/// application showed its peak alone.
enum Layer {
    Well = 0,
    Peak,
    Rms,
    Hot,
    Hold,
    Clip,
    LayerCount,
};

} // namespace

MeterItem::MeterItem(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

int MeterItem::channelCount() const
{
    return m_channelCount;
}

QList<qreal> MeterItem::rmsDb() const
{
    return m_rms;
}

QList<qreal> MeterItem::peakDb() const
{
    return m_peak;
}

QList<qreal> MeterItem::holdDb() const
{
    return m_hold;
}

bool MeterItem::isActive() const
{
    return m_active;
}

bool MeterItem::isClipped() const
{
    return m_clipped;
}

bool MeterItem::isHorizontal() const
{
    return m_horizontal;
}

QList<bool> MeterItem::clippedChannels() const
{
    return m_clippedChannels;
}

QColor MeterItem::rmsColor() const
{
    return m_rmsColor;
}

QColor MeterItem::peakColor() const
{
    return m_peakColor;
}

QColor MeterItem::holdColor() const
{
    return m_holdColor;
}

QColor MeterItem::clipColor() const
{
    return m_clipColor;
}

QColor MeterItem::wellColor() const
{
    return m_wellColor;
}

void MeterItem::setChannelCount(int count)
{
    const int wanted = std::clamp(count, 0, kMaxChannels);
    if (m_channelCount != wanted) {
        m_channelCount = wanted;
        update();
        Q_EMIT levelsChanged();
    }
}

void MeterItem::setRmsDb(const QList<qreal> &values)
{
    if (m_rms != values) {
        m_rms = values;
        update();
        Q_EMIT levelsChanged();
    }
}

void MeterItem::setPeakDb(const QList<qreal> &values)
{
    if (m_peak != values) {
        m_peak = values;
        update();
        Q_EMIT levelsChanged();
    }
}

void MeterItem::setHoldDb(const QList<qreal> &values)
{
    if (m_hold != values) {
        m_hold = values;
        update();
        Q_EMIT levelsChanged();
    }
}

void MeterItem::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        update();
        Q_EMIT levelsChanged();
    }
}

void MeterItem::setClipped(bool clipped)
{
    if (m_clipped != clipped) {
        m_clipped = clipped;
        update();
        Q_EMIT levelsChanged();
    }
}

void MeterItem::setClippedChannels(const QList<bool> &clipped)
{
    if (m_clippedChannels != clipped) {
        m_clippedChannels = clipped;
        update();
        Q_EMIT levelsChanged();
    }
}

void MeterItem::setHorizontal(bool horizontal)
{
    if (m_horizontal != horizontal) {
        m_horizontal = horizontal;
        update();
        Q_EMIT levelsChanged();
    }
}

void MeterItem::setRmsColor(const QColor &color)
{
    if (m_rmsColor != color) {
        m_rmsColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

void MeterItem::setPeakColor(const QColor &color)
{
    if (m_peakColor != color) {
        m_peakColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

void MeterItem::setHoldColor(const QColor &color)
{
    if (m_holdColor != color) {
        m_holdColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

void MeterItem::setClipColor(const QColor &color)
{
    if (m_clipColor != color) {
        m_clipColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

void MeterItem::setWellColor(const QColor &color)
{
    if (m_wellColor != color) {
        m_wellColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

qreal MeterItem::hotDb() const
{
    return m_hotDb;
}

QColor MeterItem::hotColor() const
{
    return m_hotColor;
}

void MeterItem::setHotDb(qreal db)
{
    if (m_hotDb != db) {
        m_hotDb = db;
        update();
        Q_EMIT colorsChanged();
    }
}

void MeterItem::setHotColor(const QColor &color)
{
    if (m_hotColor != color) {
        m_hotColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

void MeterItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

qreal MeterItem::levelAt(const QList<qreal> &values, int channel)
{
    return channel < values.size() ? values.at(channel) : meterScale::kFloorDb;
}

QSGNode *MeterItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    if (width() <= 0.0 || height() <= 0.0 || m_channelCount <= 0) {
        delete node;
        return nullptr;
    }

    if (node == nullptr) {
        node = new QSGNode;
        for (int layer = 0; layer < LayerCount; ++layer) {
            node->appendChildNode(new ColourNode(Qt::transparent));
        }
    }

    std::array<ColourNode *, LayerCount> layers{};
    for (int layer = 0; layer < LayerCount; ++layer) {
        layers[static_cast<std::size_t>(layer)] = static_cast<ColourNode *>(node->childAtIndex(layer));
    }
    layers[Well]->setColour(m_wellColor);
    layers[Rms]->setColour(m_rmsColor);
    layers[Peak]->setColour(m_peakColor);
    layers[Hot]->setColour(m_hotColor);
    layers[Hold]->setColour(m_holdColor);
    layers[Clip]->setColour(m_clipColor);

    // **The clip light is above the bars**, in the well's own space, so that
    // a meter reading nothing can still be lit: a clip that vanished when
    // the sound stopped would be a clip nobody saw.
    const qreal clipSize = m_horizontal ? std::min(width() * 0.04, 6.0) : std::min(height() * 0.04, 6.0);
    const QRectF well = m_horizontal ? QRectF(0.0, 0.0, std::max(0.0, width() - clipSize - 1.0), height())
                                     : QRectF(0.0, clipSize + 1.0, width(), std::max(0.0, height() - clipSize - 1.0));

    QList<QRectF> wells;
    QList<QRectF> clips;
    QList<QRectF> rms;
    QList<QRectF> peaks;
    QList<QRectF> holds;
    QList<QRectF> hots;
    wells.reserve(m_channelCount);
    rms.reserve(m_channelCount);
    peaks.reserve(m_channelCount);
    holds.reserve(m_channelCount);

    const qreal span = m_horizontal ? well.height() : well.width();
    const qreal each = span / m_channelCount;
    // A hair between the bars, and never so much that a narrow meter
    // disappears into gaps.
    const qreal gap = std::min(each * 0.15, 2.0);

    for (int channel = 0; channel < m_channelCount; ++channel) {
        const qreal offset = channel * each;
        const QRectF lane = m_horizontal
            ? QRectF(well.left(), well.top() + offset, well.width(), std::max(0.0, each - gap))
            : QRectF(well.left() + offset, well.top(), std::max(0.0, each - gap), well.height());
        wells.append(lane);

        // **Before the idle test, on purpose.** A clip light that went out
        // when the sound stopped would be a clip nobody saw.
        if (channel < m_clippedChannels.size() && m_clippedChannels.at(channel)) {
            clips.append(m_horizontal ? QRectF(width() - clipSize, lane.top(), clipSize, lane.height())
                                      : QRectF(lane.left(), 0.0, lane.width(), clipSize));
        }

        if (!m_active) {
            // **Idle is not silence.** Nothing is drawn in the lane at all,
            // so a stopped session looks switched off rather than quiet.
            continue;
        }

        const auto barOf = [&lane, this](qreal db) {
            const qreal share = meterScale::positionOf(static_cast<float>(db));
            return m_horizontal
                ? QRectF(lane.left(), lane.top(), lane.width() * share, lane.height())
                : QRectF(lane.left(), lane.bottom() - lane.height() * share, lane.width(), lane.height() * share);
        };

        if (const qreal level = levelAt(m_rms, channel); level > meterScale::kFloorDb) {
            rms.append(barOf(level));
        }
        if (const qreal level = levelAt(m_peak, channel); level > meterScale::kFloorDb) {
            peaks.append(barOf(level));
        }
        // **The hot part is the part above the level**, of whichever bar
        // reaches higher: red where the sound is, not a stripe painted at a
        // fixed place whether anything reaches it or not.
        if (const qreal top = std::max(levelAt(m_rms, channel), levelAt(m_peak, channel)); top > m_hotDb) {
            const QRectF upper = barOf(top);
            const QRectF lower = barOf(m_hotDb);
            hots.append(m_horizontal ? QRectF(lower.right(), lane.top(), upper.right() - lower.right(), lane.height())
                                     : QRectF(lane.left(), upper.top(), lane.width(), lower.top() - upper.top()));
        }
        if (const qreal held = levelAt(m_hold, channel); held > meterScale::kFloorDb) {
            const QRectF bar = barOf(held);
            holds.append(m_horizontal ? QRectF(std::max(lane.left(), bar.right() - kHoldThickness),
                                               lane.top(),
                                               kHoldThickness,
                                               lane.height())
                                      : QRectF(lane.left(), bar.top(), lane.width(), kHoldThickness));
        }
    }

    // **The peak tip is drawn first and the RMS bar over it**, so the
    // brighter colour is only the part above the bar: two bars stacked
    // would need no blending and no second pass. Which is drawn first is the
    // order of the layers above, not the order of these calls.
    layers[Well]->setRectangles(wells);
    layers[Peak]->setRectangles(peaks);
    layers[Rms]->setRectangles(rms);
    layers[Hot]->setRectangles(hots);
    layers[Hold]->setRectangles(holds);
    // One dot a side where the sides are known, and one strip across the
    // whole meter where they are not.
    if (m_clippedChannels.isEmpty()) {
        clips = m_clipped ? QList<QRectF>{m_horizontal ? QRectF(width() - clipSize, 0.0, clipSize, height())
                                                       : QRectF(0.0, 0.0, width(), clipSize)}
                          : QList<QRectF>{};
    }
    layers[Clip]->setRectangles(clips);
    return node;
}

} // namespace rb::ui

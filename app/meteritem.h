// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QColor>
#include <QQuickItem>

#include <array>

namespace rb::ui
{

/// One level meter, drawn on the scene graph.
///
/// **Scene-graph nodes, not Canvas.** A meter changes sixty times a second
/// and there may be fifty of them; a level change here costs one geometry
/// update and no new items, no bindings and no text layout. Canvas would
/// rasterise each of them on the GUI thread every frame (spike S3,
/// [ADR-0009](../docs/adr/0009-canvas-rendering.md)).
///
/// **The levels are given to it, not fetched by it.** What a meter shows is
/// MeterHub's business — it is the one place the hold and the decay are
/// worked out, once for the whole session rather than once per meter — so
/// this item takes numbers and draws them.
///
/// **Idle is not silence.** A session that is not running and a session
/// playing nothing look completely different: silence is a meter at the
/// bottom, and idle is a meter that is switched off. A person glancing at a
/// window has to be able to tell which without reading anything.
class MeterItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    /// How many bars. Above kMaxChannels the rest are not drawn, because a
    /// meter too narrow to read is not a meter.
    Q_PROPERTY(int channelCount READ channelCount WRITE setChannelCount NOTIFY levelsChanged FINAL)
    /// The bar itself, in dBFS. A list, one per channel.
    Q_PROPERTY(QList<qreal> rmsDb READ rmsDb WRITE setRmsDb NOTIFY levelsChanged FINAL)
    /// The brighter tip: the loudest sample since the last frame.
    Q_PROPERTY(QList<qreal> peakDb READ peakDb WRITE setPeakDb NOTIFY levelsChanged FINAL)
    /// The line that holds and falls, worked out in wall-clock time.
    Q_PROPERTY(QList<qreal> holdDb READ holdDb WRITE setHoldDb NOTIFY levelsChanged FINAL)

    /// Whether anything is arriving at all. False draws the idle state.
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY levelsChanged FINAL)
    /// Whether the block has reached full scale lately.
    Q_PROPERTY(bool clipped READ isClipped WRITE setClipped NOTIFY levelsChanged FINAL)
    /// Which channels did, one flag each. **A dot per side when there is
    /// room for two**; while the list is empty the whole meter carries one
    /// strip instead, which is what a meter too narrow to divide wants.
    Q_PROPERTY(QList<bool> clippedChannels READ clippedChannels WRITE setClippedChannels NOTIFY levelsChanged FINAL)

    Q_PROPERTY(bool horizontal READ isHorizontal WRITE setHorizontal NOTIFY levelsChanged FINAL)

    Q_PROPERTY(QColor rmsColor READ rmsColor WRITE setRmsColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor peakColor READ peakColor WRITE setPeakColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor holdColor READ holdColor WRITE setHoldColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor clipColor READ clipColor WRITE setClipColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor wellColor READ wellColor WRITE setWellColor NOTIFY colorsChanged FINAL)
    /// Where a bar turns hot, in dBFS: whatever part of it is above this is
    /// drawn in hotColor. Above full scale, as it is unless a meter asks,
    /// nothing is.
    Q_PROPERTY(qreal hotDb READ hotDb WRITE setHotDb NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor hotColor READ hotColor WRITE setHotColor NOTIFY colorsChanged FINAL)

public:
    /// The most bars one meter draws. Eight is what the telemetry carries.
    static constexpr int kMaxChannels = 8;
    /// How thick the hold line is, in device-independent pixels.
    static constexpr qreal kHoldThickness = 2.0;

    explicit MeterItem(QQuickItem *parent = nullptr);

    [[nodiscard]] int channelCount() const;
    [[nodiscard]] QList<qreal> rmsDb() const;
    [[nodiscard]] QList<qreal> peakDb() const;
    [[nodiscard]] QList<qreal> holdDb() const;
    [[nodiscard]] bool isActive() const;
    [[nodiscard]] bool isClipped() const;
    [[nodiscard]] QList<bool> clippedChannels() const;
    [[nodiscard]] bool isHorizontal() const;
    [[nodiscard]] QColor rmsColor() const;
    [[nodiscard]] QColor peakColor() const;
    [[nodiscard]] QColor holdColor() const;
    [[nodiscard]] QColor clipColor() const;
    [[nodiscard]] QColor wellColor() const;
    [[nodiscard]] qreal hotDb() const;
    [[nodiscard]] QColor hotColor() const;

    void setChannelCount(int count);
    void setRmsDb(const QList<qreal> &values);
    void setPeakDb(const QList<qreal> &values);
    void setHoldDb(const QList<qreal> &values);
    void setActive(bool active);
    void setClipped(bool clipped);
    void setClippedChannels(const QList<bool> &clipped);
    void setHorizontal(bool horizontal);
    void setRmsColor(const QColor &color);
    void setPeakColor(const QColor &color);
    void setHoldColor(const QColor &color);
    void setClipColor(const QColor &color);
    void setWellColor(const QColor &color);
    void setHotDb(qreal db);
    void setHotColor(const QColor &color);

Q_SIGNALS:
    void levelsChanged();
    void colorsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    /// Reads one channel out of a list, or the floor when the list is short.
    [[nodiscard]] static qreal levelAt(const QList<qreal> &values, int channel);

    int m_channelCount = 2;
    QList<qreal> m_rms;
    QList<qreal> m_peak;
    QList<qreal> m_hold;
    QList<bool> m_clippedChannels;
    bool m_active = false;
    bool m_clipped = false;
    bool m_horizontal = false;

    QColor m_rmsColor{QStringLiteral("#3daee9")};
    QColor m_peakColor{QStringLiteral("#93cee9")};
    QColor m_holdColor{QStringLiteral("#eff0f1")};
    QColor m_clipColor{QStringLiteral("#da4453")};
    QColor m_wellColor{QStringLiteral("#232629")};
    /// Above full scale: off.
    qreal m_hotDb = 1.0;
    QColor m_hotColor{QStringLiteral("#da4453")};
};

} // namespace rb::ui

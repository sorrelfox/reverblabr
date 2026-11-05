// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "spectrumitem.h"

#include "tracegeometry.h"

#include <QSGNode>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

constexpr double kDefaultRate = 48000.0;
/// What the grid marks across each scale, and which of them are named.
struct FrequencyMark {
    double hz;
    bool labelled;
};
constexpr std::array kLogarithmicMarks{FrequencyMark{50.0, false},
                                       FrequencyMark{100.0, true},
                                       FrequencyMark{200.0, false},
                                       FrequencyMark{500.0, false},
                                       FrequencyMark{1000.0, true},
                                       FrequencyMark{2000.0, false},
                                       FrequencyMark{5000.0, false},
                                       FrequencyMark{10000.0, true}};
constexpr std::array kLinearMarks{FrequencyMark{5000.0, true},
                                  FrequencyMark{10000.0, true},
                                  FrequencyMark{15000.0, true}};
/// How far apart the level marks are, in dB.
constexpr double kLevelStepDb = 20.0;
/// A grid under this width, a tile, keeps to the named marks.
constexpr float kFewerMarksBelow = 240.0f;
/// The width at which a bar gets a gap beside it, in pixels.
constexpr float kGapFromPitch = 3.0f;

[[nodiscard]] dsp::BarScale scaleOf(const QString &scale)
{
    return scale == "linear"_L1 ? dsp::BarScale::Linear : dsp::BarScale::Logarithmic;
}

} // namespace

SpectrumItem::SpectrumItem(QQuickItem *parent)
    : ScopeSurface(parent)
{
    m_settings.everySample = true;
}

SpectrumItem::~SpectrumItem() = default;

void SpectrumItem::startAfresh()
{
    m_view.clear();
}

int SpectrumItem::bars() const
{
    return m_bars;
}

void SpectrumItem::setBars(int bars)
{
    if (bars > 0 && bars != m_bars) {
        m_bars = bars;
        changeSettings();
        sayBarsShown();
    }
}

QString SpectrumItem::frequencyScale() const
{
    return m_scale;
}

void SpectrumItem::setFrequencyScale(const QString &scale)
{
    if (scale != m_scale) {
        m_scale = scale;
        m_spectrum.scale = scaleOf(scale);
        changeSettings();
        Q_EMIT marksChanged();
    }
}

int SpectrumItem::frameSize() const
{
    return static_cast<int>(m_spectrum.frameSize);
}

void SpectrumItem::setFrameSize(int frames)
{
    if (frames > 0 && static_cast<std::size_t>(frames) != m_spectrum.frameSize) {
        m_spectrum.frameSize = static_cast<std::size_t>(frames);
        changeSettings();
        Q_EMIT readingsChanged();
    }
}

qreal SpectrumItem::floorDb() const
{
    return m_floorDb;
}

void SpectrumItem::setFloorDb(qreal db)
{
    if (db < 0.0 && db != m_floorDb) {
        // Nothing but the drawing depends on it.
        m_floorDb = db;
        update();
        Q_EMIT settingsChanged();
        Q_EMIT marksChanged();
    }
}

qreal SpectrumItem::tiltDb() const
{
    return m_spectrum.tiltDb;
}

void SpectrumItem::setTiltDb(qreal db)
{
    if (db != m_spectrum.tiltDb) {
        m_spectrum.tiltDb = db;
        changeSettings();
    }
}

bool SpectrumItem::peakMarks() const
{
    return m_peakMarks;
}

void SpectrumItem::setPeakMarks(bool on)
{
    if (on != m_peakMarks) {
        m_peakMarks = on;
        update();
        Q_EMIT settingsChanged();
    }
}

QString SpectrumItem::channels() const
{
    return m_channels;
}

void SpectrumItem::setChannels(const QString &channels)
{
    if (channels != m_channels) {
        m_channels = channels;
        m_spectrum.trace = channels == "left"_L1 ? rt::ScopeLeft
            : channels == "right"_L1             ? rt::ScopeRight
                                                 : rt::ScopeMid;
        changeSettings();
    }
}

bool SpectrumItem::grid() const
{
    return m_grid;
}

void SpectrumItem::setGrid(bool on)
{
    if (on != m_grid) {
        m_grid = on;
        update();
        Q_EMIT settingsChanged();
    }
}

QString SpectrumItem::barStyle() const
{
    return m_style;
}

void SpectrumItem::setBarStyle(const QString &style)
{
    if (style != m_style) {
        // Nothing but the drawing depends on it.
        m_style = style;
        update();
        Q_EMIT settingsChanged();
    }
}

QColor SpectrumItem::barColor() const
{
    return m_barColor;
}

void SpectrumItem::setBarColor(const QColor &color)
{
    if (color != m_barColor) {
        m_barColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

QColor SpectrumItem::peakColor() const
{
    return m_peakColor;
}

void SpectrumItem::setPeakColor(const QColor &color)
{
    if (color != m_peakColor) {
        m_peakColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

QColor SpectrumItem::gridColor() const
{
    return m_gridColor;
}

void SpectrumItem::setGridColor(const QColor &color)
{
    if (color != m_gridColor) {
        m_gridColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

int SpectrumItem::barsShown() const
{
    return std::min(m_bars, static_cast<int>(width()) / kLeastPitch);
}

double SpectrumItem::sampleRate() const
{
    return feed() != nullptr && feed()->spectrum().sampleRate() > 0 ? feed()->spectrum().sampleRate() : 0.0;
}

qreal SpectrumItem::frameMs() const
{
    const double rate = sampleRate();
    return rate > 0.0 ? 1000.0 * static_cast<double>(m_spectrum.frameSize) / rate : 0.0;
}

qreal SpectrumItem::resolutionHz() const
{
    // The window's hump is two bins across at half its height.
    const double rate = sampleRate();
    return rate / static_cast<double>(m_spectrum.frameSize) * 2.0;
}

std::vector<SpectrumItem::Mark> SpectrumItem::marksAcross() const
{
    const double rate = m_saidRate > 0.0 ? m_saidRate : kDefaultRate;
    const double top = dsp::BarMap::highestHz(rate);
    std::vector<Mark> marks;
    const auto add = [&](const auto &list) {
        for (const FrequencyMark &mark : list) {
            if (mark.hz < top) {
                marks.push_back({mark.hz, dsp::BarMap::positionOf(mark.hz, m_spectrum.scale, rate), mark.labelled});
            }
        }
    };
    if (m_spectrum.scale == dsp::BarScale::Linear) {
        add(kLinearMarks);
    } else {
        add(kLogarithmicMarks);
    }
    return marks;
}

std::vector<SpectrumItem::Mark> SpectrumItem::marksUp() const
{
    std::vector<Mark> marks;
    for (double db = -kLevelStepDb; db > m_floorDb; db -= kLevelStepDb) {
        marks.push_back({db, SpectrumView::heightOf(static_cast<float>(db), m_floorDb), true});
    }
    return marks;
}

QVariantList SpectrumItem::frequencyMarks() const
{
    QVariantList list;
    for (const Mark &mark : marksAcross()) {
        list.append(QVariantMap{{u"hz"_s, mark.value}, {u"position"_s, mark.position}, {u"labelled"_s, mark.labelled}});
    }
    return list;
}

QVariantList SpectrumItem::levelMarks() const
{
    QVariantList list;
    for (const Mark &mark : marksUp()) {
        list.append(QVariantMap{{u"db"_s, mark.value}, {u"position"_s, mark.position}});
    }
    return list;
}

QVariantList SpectrumItem::levels() const
{
    QVariantList list;
    for (const float level : m_view.levels()) {
        list.append(static_cast<qreal>(level));
    }
    return list;
}

QVariantList SpectrumItem::peakLevels() const
{
    QVariantList list;
    for (const float level : m_view.peaks()) {
        list.append(static_cast<qreal>(level));
    }
    return list;
}

int SpectrumItem::barOf(qreal hz) const
{
    return m_view.bars() > 0 ? static_cast<int>(m_view.map().barOf(hz)) : -1;
}

bool SpectrumItem::workOutPicture()
{
    ScopeFeed *from = feed();
    SpectrumSettings settings = m_spectrum;
    settings.refreshHz = m_settings.refreshHz;
    // Held, the picture is still worked out, from what is kept, so that a
    // setting changed meanwhile still shows; it just never moves on.
    settings.smooth = m_settings.smooth && !hold();
    settings.bars = static_cast<std::size_t>(std::max(barsShown(), 0));
    from->spectrum().update(from->frame(), settings.frameSize, settings.trace);
    return m_view.update(from->spectrum(), settings, hub()->nowMs());
}

void SpectrumItem::sayOwnReadings()
{
    const double rate = sampleRate();
    if (rate != m_saidRate) {
        m_saidRate = rate;
        Q_EMIT readingsChanged();
        Q_EMIT marksChanged();
    }
}

void SpectrumItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    ScopeSurface::geometryChange(newGeometry, oldGeometry);
    sayBarsShown();
}

void SpectrumItem::sayBarsShown()
{
    if (barsShown() != m_saidBarsShown) {
        m_saidBarsShown = barsShown();
        Q_EMIT barsShownChanged();
    }
}

QSGNode *SpectrumItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    if (width() <= 0.0 || height() <= 0.0) {
        delete node;
        return nullptr;
    }
    if (node == nullptr) {
        node = new QSGNode;
        for (int child = 0; child < 3; ++child) {
            node->appendChildNode(new TraceNode); // The grid, the bars, the marks.
            node->childAtIndex(child)->setFlag(QSGNode::OwnedByParent);
        }
    }
    auto *gridNode = static_cast<TraceNode *>(node->childAtIndex(0));
    auto *barNode = static_cast<TraceNode *>(node->childAtIndex(1));
    auto *markNode = static_cast<TraceNode *>(node->childAtIndex(2));

    const bool showing = isShowing();
    if (showing) {
        pictureWorkedOut(workOutPicture());
    }

    const auto w = static_cast<float>(width());
    const auto h = static_cast<float>(height());

    Triangles triangles;
    if (m_grid) {
        triangles.setColor(m_gridColor);
        for (const Mark &mark : marksAcross()) {
            if (w >= kFewerMarksBelow || mark.labelled) {
                const float x = std::round(static_cast<float>(mark.position) * w);
                triangles.rectangle(x, 0.0f, x + 1.0f, h, 1.0f);
            }
        }
        for (const Mark &mark : marksUp()) {
            const float y = std::round((1.0f - static_cast<float>(mark.position)) * h);
            triangles.rectangle(0.0f, y, w, y + 1.0f, 1.0f);
        }
    }
    gridNode->setTriangles(triangles.vertices(), triangles.indices());
    int vertices = static_cast<int>(triangles.vertices().size());

    const std::span<const float> levels = showing ? m_view.levels() : std::span<const float>();
    const float pitch = levels.empty() ? 0.0f : w / static_cast<float>(levels.size());
    const float gap = pitch >= kGapFromPitch ? 1.0f : 0.0f;
    const bool segmented = m_style == "segmented"_L1;

    triangles.clear();
    triangles.setColor(m_barColor);
    for (std::size_t bar = 0; bar < levels.size(); ++bar) {
        const float height = SpectrumView::heightOf(levels[bar], m_floorDb) * h;
        const float left = std::round(static_cast<float>(bar) * pitch);
        const float right = std::round(static_cast<float>(bar + 1) * pitch) - gap;
        if (!segmented) {
            if (height > 0.0f) {
                triangles.rectangle(left, h - height, right, h, 1.0f);
            }
            continue;
        }
        // Lit from the bottom, one segment for any part of it reached.
        const int lit = static_cast<int>(std::ceil(height / kSegmentPitch));
        for (int segment = 0; segment < lit; ++segment) {
            const float bottom = h - static_cast<float>(segment) * kSegmentPitch;
            triangles.rectangle(left, bottom - kSegmentPitch + 1.0f, right, bottom, 1.0f);
        }
    }
    barNode->setTriangles(triangles.vertices(), triangles.indices());
    vertices += static_cast<int>(triangles.vertices().size());

    triangles.clear();
    const std::span<const float> peaks = m_peakMarks && showing ? m_view.peaks() : std::span<const float>();
    triangles.setColor(m_peakColor);
    for (std::size_t bar = 0; bar < peaks.size(); ++bar) {
        const float height = SpectrumView::heightOf(peaks[bar], m_floorDb) * h;
        if (height > 0.0f) {
            const float left = std::round(static_cast<float>(bar) * pitch);
            const float right = std::round(static_cast<float>(bar + 1) * pitch) - gap;
            const float top = std::min(h - height, h - kPeakThickness);
            triangles.rectangle(left, top, right, top + kPeakThickness, 1.0f);
        }
    }
    markNode->setTriangles(triangles.vertices(), triangles.indices());
    vertices += static_cast<int>(triangles.vertices().size());

    frameBuilt(vertices);
    return node;
}

} // namespace rb::ui

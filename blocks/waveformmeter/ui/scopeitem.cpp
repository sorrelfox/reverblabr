// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scopeitem.h"

#include "tracegeometry.h"

#include <QSGNode>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// How thick a line is and how far its edge fades, in pixels.
constexpr float kLineThickness = 1.5f;
constexpr float kEdgeFade = 1.0f;
/// How strong a filled trace is, and how many divisions the grid has.
constexpr float kFillStrength = 0.65f;
constexpr int kGridDivisions = 8;
/// A tile has room for half as many.
constexpr float kFewerDivisionsBelow = 240.0f;
constexpr float kDotSize = 2.5f;
/// What the bottom of the Decibels scale is marked at.
constexpr std::array<float, 4> kDecibelMarks{-12.0f, -24.0f, -36.0f, -48.0f};

/// Where a height goes in a lane of the item.
struct Lane {
    float middle = 0.0f;
    float reach = 0.0f;

    [[nodiscard]] float yOf(float height) const noexcept
    {
        return middle - height * reach;
    }
};

/// How far above or below the middle a column reaches.
[[nodiscard]] float reachOf(const dsp::ScopeColumn &column) noexcept
{
    return std::max(std::fabs(column.low), std::fabs(column.high));
}

/// A band from \a top to \a bottom at each column, edges faded: what a line
/// and a filled trace both are. One vertex row a column, so that the middle
/// of a long trace shares its vertices with its neighbours.
struct BandColumn {
    float x = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    bool heard = false;
};

void addBand(Triangles &triangles, const std::vector<BandColumn> &band, float strength)
{
    quint32 previous = 0;
    bool joined = false;
    for (const BandColumn &column : band) {
        if (!column.heard) {
            joined = false;
            continue;
        }
        const quint32 first = triangles.vertex(column.x, column.top - kEdgeFade, 0.0f);
        (void)triangles.vertex(column.x, column.top, strength);
        (void)triangles.vertex(column.x, column.bottom, strength);
        (void)triangles.vertex(column.x, column.bottom + kEdgeFade, 0.0f);
        if (joined) {
            for (quint32 row = 0; row < 3; ++row) {
                triangles.quad(previous + row, first + row, previous + row + 1, first + row + 1);
            }
        }
        previous = first;
        joined = true;
    }
}

} // namespace

ScopeItem::ScopeItem(QQuickItem *parent)
    : ScopeSurface(parent)
{
}

ScopeItem::~ScopeItem() = default;

void ScopeItem::startAfresh()
{
    m_view.clear();
}

bool ScopeItem::workOutPicture()
{
    // Held, the picture is still worked out, from what is kept, so that a
    // setting changed meanwhile still shows; it just never moves on.
    ScopeSettings settings = m_settings;
    if (hold()) {
        settings.smooth = false;
    }
    return m_view.update(feed()->frame(), settings, static_cast<std::size_t>(columnCount()), hub()->nowMs());
}

QString ScopeItem::style() const
{
    return m_style;
}

void ScopeItem::setStyle(const QString &style)
{
    if (style != m_style) {
        // Nothing but the drawing depends on it: the next frame is the new
        // style, from the sound already here.
        m_style = style;
        update();
        Q_EMIT settingsChanged();
    }
}

qreal ScopeItem::spanMs() const
{
    return m_settings.spanMs;
}

void ScopeItem::setSpanMs(qreal ms)
{
    if (ms > 0.0 && ms != m_settings.spanMs) {
        m_settings.spanMs = ms;
        changeSettings();
    }
}

qreal ScopeItem::gainDb() const
{
    return m_settings.gainDb;
}

void ScopeItem::setGainDb(qreal db)
{
    if (db != m_settings.gainDb) {
        m_settings.gainDb = db;
        changeSettings();
    }
}

bool ScopeItem::autoFit() const
{
    return m_settings.autoFit;
}

void ScopeItem::setAutoFit(bool on)
{
    if (on != m_settings.autoFit) {
        m_settings.autoFit = on;
        changeSettings();
    }
}

QString ScopeItem::channels() const
{
    return m_channels;
}

void ScopeItem::setChannels(const QString &channels)
{
    if (channels == m_channels) {
        return;
    }
    m_channels = channels;
    m_settings.channels = channels == "left"_L1 ? ScopeSettings::Channels::Left
        : channels == "right"_L1                ? ScopeSettings::Channels::Right
        : channels == "mid"_L1                  ? ScopeSettings::Channels::Mid
        : channels == "side"_L1                 ? ScopeSettings::Channels::Side
        : channels == "stacked"_L1              ? ScopeSettings::Channels::Stacked
                                                : ScopeSettings::Channels::Both;
    changeSettings();
}

bool ScopeItem::trigger() const
{
    return m_settings.trigger;
}

void ScopeItem::setTrigger(bool on)
{
    if (on != m_settings.trigger) {
        m_settings.trigger = on;
        changeSettings();
    }
}

bool ScopeItem::grid() const
{
    return m_grid;
}

void ScopeItem::setGrid(bool on)
{
    if (on != m_grid) {
        m_grid = on;
        update();
        Q_EMIT settingsChanged();
    }
}

QString ScopeItem::heightScale() const
{
    return m_scale;
}

void ScopeItem::setHeightScale(const QString &scale)
{
    if (scale != m_scale) {
        m_scale = scale;
        m_settings.decibels = scale == "decibels"_L1;
        changeSettings();
    }
}

QColor ScopeItem::firstColor() const
{
    return m_firstColor;
}

void ScopeItem::setFirstColor(const QColor &color)
{
    if (color != m_firstColor) {
        m_firstColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

QColor ScopeItem::secondColor() const
{
    return m_secondColor;
}

void ScopeItem::setSecondColor(const QColor &color)
{
    if (color != m_secondColor) {
        m_secondColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

QColor ScopeItem::gridColor() const
{
    return m_gridColor;
}

void ScopeItem::setGridColor(const QColor &color)
{
    if (color != m_gridColor) {
        m_gridColor = color;
        update();
        Q_EMIT colorsChanged();
    }
}

bool ScopeItem::crossed() const
{
    // **What was last said**, not the view's own: the render thread works
    // the trace out and the change is said here a moment later, and a
    // property read in between would disagree with every binding on it.
    return m_saidCrossed;
}

QVariantList ScopeItem::heights(int trace) const
{
    QVariantList list;
    for (const dsp::ScopeColumn &column : m_view.trace(static_cast<std::size_t>(trace))) {
        list.append(column.heard ? QVariant(static_cast<qreal>(column.low)) : QVariant());
        list.append(column.heard ? QVariant(static_cast<qreal>(column.high)) : QVariant());
    }
    return list;
}

int ScopeItem::columnCount() const
{
    return std::clamp(static_cast<int>(std::ceil(width())), 0, kMaxColumns);
}

void ScopeItem::sayOwnReadings()
{
    const bool crossed = m_view.crossed();
    if (crossed != m_saidCrossed) {
        m_saidCrossed = crossed;
        Q_EMIT readingsChanged();
    }
}

QSGNode *ScopeItem::updatePaintNode(QSGNode *node, UpdatePaintNodeData *)
{
    if (width() <= 0.0 || height() <= 0.0) {
        delete node;
        return nullptr;
    }
    if (node == nullptr) {
        node = new QSGNode;
        node->appendChildNode(new TraceNode); // The grid.
        node->appendChildNode(new TraceNode); // The second trace, under the first.
        node->appendChildNode(new TraceNode);
        const auto owned = [](QSGNode *child) { child->setFlag(QSGNode::OwnedByParent); };
        owned(node->childAtIndex(0));
        owned(node->childAtIndex(1));
        owned(node->childAtIndex(2));
    }
    auto *gridNode = static_cast<TraceNode *>(node->childAtIndex(0));
    auto *secondNode = static_cast<TraceNode *>(node->childAtIndex(1));
    auto *firstNode = static_cast<TraceNode *>(node->childAtIndex(2));

    const bool showing = isShowing();
    if (showing) {
        pictureWorkedOut(workOutPicture());
    }

    const auto w = static_cast<float>(width());
    const auto h = static_cast<float>(height());
    const bool stacked = m_settings.channels == ScopeSettings::Channels::Stacked;
    const auto laneOf = [&](std::size_t trace) {
        if (!stacked) {
            return Lane{h / 2.0f, h / 2.0f - kEdgeFade};
        }
        const float laneHeight = h / 2.0f;
        return Lane{laneHeight * (static_cast<float>(trace) + 0.5f), laneHeight / 2.0f - kEdgeFade};
    };

    Triangles triangles;
    if (m_grid) {
        triangles.setColor(m_gridColor);
        const int divisions = w < kFewerDivisionsBelow ? kGridDivisions / 2 : kGridDivisions;
        for (int division = 1; division < divisions; ++division) {
            const float x = std::round(w * static_cast<float>(division) / static_cast<float>(divisions));
            triangles.rectangle(x, 0.0f, x + 1.0f, h, 1.0f);
        }
        for (std::size_t lane = 0; lane < (stacked ? 2U : 1U); ++lane) {
            const Lane place = laneOf(lane);
            const auto mark = [&](float height) {
                const float y = std::round(place.yOf(height));
                triangles.rectangle(0.0f, y, w, y + 1.0f, 1.0f);
            };
            mark(0.0f);
            if (m_settings.decibels) {
                for (const float db : kDecibelMarks) {
                    const float height = static_cast<float>(1.0 - db / ScopeView::kDecibelFloor);
                    mark(height);
                    mark(-height);
                }
            } else {
                mark(0.5f);
                mark(-0.5f);
            }
        }
    }
    gridNode->setTriangles(triangles.vertices(), triangles.indices());
    int vertices = static_cast<int>(triangles.vertices().size());

    const auto traceInto = [&](TraceNode *into, std::size_t trace) {
        triangles.clear();
        const std::span<const dsp::ScopeColumn> columns =
            showing && trace < m_view.traces() ? m_view.trace(trace) : std::span<const dsp::ScopeColumn>();
        if (!columns.empty()) {
            const Lane lane = laneOf(trace);
            const float pitch = w / static_cast<float>(m_view.columns());
            const auto shift = static_cast<float>(m_view.shift());
            const auto xOf = [&](std::size_t column) {
                return std::clamp((static_cast<float>(column) - shift) * pitch, 0.0f, w);
            };
            triangles.setColor(trace == 0 ? m_firstColor : m_secondColor);
            if (m_style == "bars"_L1 || m_style == "dots"_L1) {
                const bool bars = m_style == "bars"_L1;
                const std::size_t step = bars ? kBarPitch : kDotPitch;
                const std::uint64_t whole = m_view.firstColumn();
                triangles.reserve(columns.size() / step * 8, columns.size() / step * 12);
                // Grouped by where a column is on the sound, so that a bar
                // keeps its samples as it travels.
                std::size_t column = 0;
                while (column < columns.size()) {
                    const std::size_t group = static_cast<std::size_t>((whole + column) % step);
                    const std::size_t next = std::min(column + (step - group), columns.size());
                    float low = 2.0f;
                    float high = -2.0f;
                    float reach = 0.0f;
                    bool heard = false;
                    for (std::size_t member = column; member < next; ++member) {
                        if (columns[member].heard) {
                            heard = true;
                            low = std::min(low, columns[member].low);
                            high = std::max(high, columns[member].high);
                            reach = std::max(reach, reachOf(columns[member]));
                        }
                    }
                    if (heard) {
                        const float left = xOf(column);
                        const float right = xOf(next);
                        if (bars && right > left) {
                            const float gap = std::min(1.0f, (right - left) / 4.0f);
                            triangles.rectangle(left, lane.yOf(reach), right - gap, lane.yOf(-reach) + 1.0f, 1.0f);
                        } else if (!bars) {
                            const float x = std::clamp((left + right) / 2.0f, 0.0f, w - kDotSize);
                            const float top = lane.yOf(high) - kDotSize / 2.0f;
                            const float bottom = lane.yOf(low) - kDotSize / 2.0f;
                            triangles.rectangle(x, top, x + kDotSize, top + kDotSize, 1.0f);
                            if (bottom - top > kDotSize) {
                                triangles.rectangle(x, bottom, x + kDotSize, bottom + kDotSize, 1.0f);
                            }
                        }
                    }
                    column = next;
                }
            } else {
                const bool filled = m_style == "filled"_L1;
                std::vector<BandColumn> band(columns.size());
                triangles.reserve(band.size() * 4, band.size() * 18);
                for (std::size_t column = 0; column < band.size(); ++column) {
                    const dsp::ScopeColumn &here = columns[column];
                    BandColumn &into = band[column];
                    into.x = xOf(column);
                    into.heard = here.heard;
                    if (!here.heard) {
                        continue;
                    }
                    if (filled) {
                        // Mirrored: how far the sound reaches, either way.
                        const float reach = reachOf(here);
                        into.top = lane.yOf(reach);
                        into.bottom = lane.yOf(-reach);
                        continue;
                    }
                    // Each column reaches from one neighbour's edge to the
                    // other's, so that a steep stretch stays as thick as a
                    // level one.
                    into.top = lane.yOf(here.high) - kLineThickness / 2.0f;
                    into.bottom = lane.yOf(here.low) + kLineThickness / 2.0f;
                }
                addBand(triangles, band, filled ? kFillStrength : 1.0f);
            }
        }
        into->setTriangles(triangles.vertices(), triangles.indices());
        vertices += static_cast<int>(triangles.vertices().size());
    };
    traceInto(firstNode, 0);
    traceInto(secondNode, 1);

    frameBuilt(vertices);
    return node;
}

} // namespace rb::ui

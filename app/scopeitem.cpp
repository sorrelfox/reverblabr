// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scopeitem.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGNode>
#include <QSGVertexColorMaterial>

#include <algorithm>
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

/// One colour's worth of triangles.
///
/// A colour for each vertex and not a material for each strength, so that a
/// line can fade at its edges where the scene graph has no antialiasing to
/// lend it.
class TraceNode : public QSGGeometryNode
{
public:
    TraceNode()
        : m_geometry(QSGGeometry::defaultAttributes_ColoredPoint2D(), 0, 0, QSGGeometry::UnsignedIntType)
    {
        m_geometry.setDrawingMode(QSGGeometry::DrawTriangles);
        m_geometry.setVertexDataPattern(QSGGeometry::StreamPattern);
        m_geometry.setIndexDataPattern(QSGGeometry::StreamPattern);
        setGeometry(&m_geometry);
        setMaterial(&m_material);
    }

    void setTriangles(const std::vector<QSGGeometry::ColoredPoint2D> &vertices, const std::vector<quint32> &indices)
    {
        const auto vertexCount = static_cast<int>(vertices.size());
        const auto indexCount = static_cast<int>(indices.size());
        if (m_geometry.vertexCount() != vertexCount || m_geometry.indexCount() != indexCount) {
            m_geometry.allocate(vertexCount, indexCount);
        }
        std::ranges::copy(vertices, m_geometry.vertexDataAsColoredPoint2D());
        std::ranges::copy(indices, m_geometry.indexDataAsUInt());
        markDirty(QSGNode::DirtyGeometry);
    }

private:
    QSGGeometry m_geometry;
    QSGVertexColorMaterial m_material;
};

/// Triangles being put together.
class Triangles
{
public:
    void clear()
    {
        m_vertices.clear();
        m_indices.clear();
    }

    void reserve(std::size_t vertices, std::size_t indices)
    {
        m_vertices.reserve(vertices);
        m_indices.reserve(indices);
    }

    void setColor(const QColor &color)
    {
        m_red = color.redF();
        m_green = color.greenF();
        m_blue = color.blueF();
        m_alpha = color.alphaF();
    }

    [[nodiscard]] quint32 vertex(float x, float y, float strength)
    {
        // Premultiplied, as the material wants them.
        const float alpha = m_alpha * strength;
        QSGGeometry::ColoredPoint2D point{};
        point.set(x,
                  y,
                  static_cast<uchar>(std::lround(m_red * alpha * 255.0f)),
                  static_cast<uchar>(std::lround(m_green * alpha * 255.0f)),
                  static_cast<uchar>(std::lround(m_blue * alpha * 255.0f)),
                  static_cast<uchar>(std::lround(alpha * 255.0f)));
        m_vertices.push_back(point);
        return static_cast<quint32>(m_vertices.size() - 1);
    }

    /// One quad from two pairs of vertices already made.
    void quad(quint32 a, quint32 b, quint32 c, quint32 d)
    {
        m_indices.insert(m_indices.end(), {a, b, c, b, d, c});
    }

    void rectangle(float x1, float y1, float x2, float y2, float strength)
    {
        const quint32 a = vertex(x1, y1, strength);
        const quint32 b = vertex(x2, y1, strength);
        const quint32 c = vertex(x1, y2, strength);
        const quint32 d = vertex(x2, y2, strength);
        quad(a, b, c, d);
    }

    [[nodiscard]] const std::vector<QSGGeometry::ColoredPoint2D> &vertices() const
    {
        return m_vertices;
    }
    [[nodiscard]] const std::vector<quint32> &indices() const
    {
        return m_indices;
    }

private:
    std::vector<QSGGeometry::ColoredPoint2D> m_vertices;
    std::vector<quint32> m_indices;
    float m_red = 1.0f;
    float m_green = 1.0f;
    float m_blue = 1.0f;
    float m_alpha = 1.0f;
};

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
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

ScopeItem::~ScopeItem()
{
    adopt(nullptr);
}

ScopeHub *ScopeItem::hub() const
{
    return m_hub;
}

void ScopeItem::setHub(ScopeHub *hub)
{
    if (hub != m_hub) {
        adopt(nullptr);
        m_hub = hub;
        Q_EMIT sourceChanged();
        follow();
    }
}

QString ScopeItem::blockId() const
{
    return m_blockId;
}

void ScopeItem::setBlockId(const QString &blockId)
{
    if (blockId != m_blockId) {
        adopt(nullptr);
        m_blockId = blockId;
        m_view.clear();
        Q_EMIT sourceChanged();
        follow();
    }
}

void ScopeItem::changeSettings()
{
    if (m_feed) {
        m_feed->setSettings(m_settings);
    }
    followFrames();
    update();
    Q_EMIT settingsChanged();
}

qreal ScopeItem::refreshHz() const
{
    return m_settings.refreshHz;
}

void ScopeItem::setRefreshHz(qreal hz)
{
    if (hz > 0.0 && hz != m_settings.refreshHz) {
        m_settings.refreshHz = hz;
        changeSettings();
    }
}

QString ScopeItem::motion() const
{
    return m_motion;
}

void ScopeItem::setMotion(const QString &motion)
{
    if (motion != m_motion) {
        m_motion = motion;
        m_settings.smooth = motion == "smooth"_L1;
        changeSettings();
    }
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

bool ScopeItem::hold() const
{
    return m_hold;
}

void ScopeItem::setHold(bool on)
{
    if (on != m_hold) {
        m_hold = on;
        if (m_feed) {
            m_feed->setHold(on);
        }
        followFrames();
        update();
        Q_EMIT settingsChanged();
        Q_EMIT liveChanged();
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

QString ScopeItem::feedState() const
{
    if (!m_feed) {
        return u"stopped"_s;
    }
    switch (m_feed->state()) {
    case ScopeFeed::State::Live:
        return u"live"_s;
    case ScopeFeed::State::Waiting:
        return u"waiting"_s;
    case ScopeFeed::State::NoRoom:
        return u"noRoom"_s;
    case ScopeFeed::State::Stopped:
        return u"stopped"_s;
    case ScopeFeed::State::Off:
        return u"off"_s;
    case ScopeFeed::State::Overloaded:
        return u"overloaded"_s;
    case ScopeFeed::State::NoNews:
        return u"noNews"_s;
    }
    return u"stopped"_s;
}

bool ScopeItem::isShowing() const
{
    return m_feed && m_feed->hasTrace();
}

bool ScopeItem::crossed() const
{
    return m_view.crossed();
}

qreal ScopeItem::arrivalsPerSecond() const
{
    return m_feed ? m_feed->arrivalsPerSecond() : 0.0;
}

int ScopeItem::vertexCount() const
{
    return m_vertexCount;
}

int ScopeItem::pictures() const
{
    return m_pictures;
}

bool ScopeItem::isTicking() const
{
    return m_ticking;
}

bool ScopeItem::isSubscribed() const
{
    return !m_feed.isNull();
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

void ScopeItem::work()
{
    if (m_feed) {
        m_feed->tick();
        if (m_view.update(m_feed->frame(), m_settings, static_cast<std::size_t>(columnCount()), m_hub->nowMs())) {
            ++m_pictures;
        }
        sayReadings();
    }
}

void ScopeItem::adopt(ScopeFeed *feed)
{
    if (feed == m_feed) {
        return;
    }
    if (m_feed) {
        disconnect(m_feed, nullptr, this, nullptr);
        if (m_hub) {
            m_hub->unsubscribe(m_feed);
        }
    }
    m_feed = feed;
    if (m_feed) {
        m_feed->setSettings(m_settings);
        m_feed->setHold(m_hold);
        connect(m_feed, &ScopeFeed::refreshed, this, &ScopeItem::onRefreshed);
        connect(m_feed, &ScopeFeed::stateChanged, this, &ScopeItem::onStateChanged);
        m_view.clear();
    }
    followFrames();
    Q_EMIT liveChanged();
}

bool ScopeItem::isOnScreen() const
{
    return isVisible() && window() != nullptr && window()->isVisible() && m_hub && !m_blockId.isEmpty();
}

void ScopeItem::follow()
{
    if (isOnScreen()) {
        if (!m_feed) {
            adopt(m_hub->subscribe(m_blockId));
        }
    } else if (m_feed && !m_lettingGo) {
        m_lettingGo = true;
        QMetaObject::invokeMethod(this, &ScopeItem::letGoUnlessShown, Qt::QueuedConnection);
    }
    update();
}

void ScopeItem::letGoUnlessShown()
{
    m_lettingGo = false;
    if (!isOnScreen()) {
        adopt(nullptr);
    }
}

void ScopeItem::followWindow(QQuickWindow *window)
{
    disconnect(m_windowShown);
    disconnect(m_frames);
    m_ticking = false;
    m_window = window;
    if (window != nullptr) {
        m_windowShown = connect(window, &QWindow::visibleChanged, this, &ScopeItem::follow);
    }
    follow();
    followFrames();
}

void ScopeItem::followFrames()
{
    // **Only while a frame could show something new.** Asking for the next
    // frame is what makes a window draw continuously, on the GPU and all.
    const bool wanted = m_settings.smooth && !m_hold && m_feed && m_feed->hasTrace() && m_window != nullptr;
    if (wanted == m_ticking) {
        return;
    }
    m_ticking = wanted;
    if (wanted) {
        // **Queued**: this signal comes from the window's render thread, and
        // update() belongs to this one.
        m_frames =
            connect(m_window, &QQuickWindow::frameSwapped, this, &ScopeItem::onFrameSwapped, Qt::QueuedConnection);
        update();
    } else {
        disconnect(m_frames);
    }
    Q_EMIT liveChanged();
}

void ScopeItem::onRefreshed()
{
    followFrames();
    update();
}

void ScopeItem::onStateChanged()
{
    followFrames();
    update();
    Q_EMIT liveChanged();
}

void ScopeItem::sayReadings()
{
    const bool crossed = m_view.crossed();
    const qreal arrivals = arrivalsPerSecond();
    if (crossed != m_saidCrossed || arrivals != m_saidArrivals) {
        m_saidCrossed = crossed;
        m_saidArrivals = arrivals;
        Q_EMIT readingsChanged();
    }
}

void ScopeItem::onFrameSwapped()
{
    if (m_ticking) {
        update();
    }
}

void ScopeItem::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

void ScopeItem::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange) {
        followWindow(value.window);
    } else if (change == ItemVisibleHasChanged) {
        follow();
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

    const bool showing = m_feed && m_feed->hasTrace();
    if (showing) {
        const auto columns = static_cast<std::size_t>(columnCount());
        // Held, the picture is still worked out, from what is kept, so that a
        // setting changed meanwhile still shows; it just never moves on.
        ScopeSettings settings = m_settings;
        if (m_hold) {
            settings.smooth = false;
        }
        if (m_view.update(m_feed->frame(), settings, columns, m_hub->nowMs())) {
            ++m_pictures;
        }
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

    m_vertexCount = vertices;
    // On this item's own thread, after the frame.
    QMetaObject::invokeMethod(this, &ScopeItem::sayReadings, Qt::QueuedConnection);
    return node;
}

} // namespace rb::ui

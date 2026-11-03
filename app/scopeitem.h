// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "scopehub.h"
#include "scopeview.h"

#include <QColor>
#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>
#include <QString>
#include <QVariantList>

namespace rb::ui
{

/// **The sound at a point in a lane, drawn on the scene graph.**
///
/// Scene-graph geometry and not Canvas, for the reason MeterItem gives, and
/// more so: this redraws a whole trace, maybe at every frame.
///
/// **It holds nothing about the window it is in.** The card moves between
/// windows as it goes outside the main one and back, and each window has its
/// own render loop, so the nodes belong to whichever window draws them and are
/// built from nothing if asked, and what ticks it is looked up again when the
/// window changes. Everything that lasts is in the feed and the view.
///
/// **Nothing ticks while nobody can see it.** It holds a feed only while
/// visible in a shown window, and that is what keeps the block's sound coming;
/// and it asks to be drawn at each frame of the screen only while Smooth is on
/// and there is a trace to move.
///
/// **The feed is let go of a moment later and not at once**, because a card
/// going from one window to another belongs to no window for an instant, and
/// the feed is where its trace is.
class ScopeItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    /// Where the sound comes from: Meters.scopes.
    Q_PROPERTY(rb::ui::ScopeHub *hub READ hub WRITE setHub NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QString blockId READ blockId WRITE setBlockId NOTIFY sourceChanged FINAL)

    /// The block's settings, as they are spelled in the session.
    Q_PROPERTY(qreal refreshHz READ refreshHz WRITE setRefreshHz NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString motion READ motion WRITE setMotion NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString style READ style WRITE setStyle NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal spanMs READ spanMs WRITE setSpanMs NOTIFY settingsChanged FINAL)
    Q_PROPERTY(qreal gainDb READ gainDb WRITE setGainDb NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool autoFit READ autoFit WRITE setAutoFit NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString channels READ channels WRITE setChannels NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool trigger READ trigger WRITE setTrigger NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool grid READ grid WRITE setGrid NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString heightScale READ heightScale WRITE setHeightScale NOTIFY settingsChanged FINAL)

    /// Left, or the one channel shown; and right.
    Q_PROPERTY(QColor firstColor READ firstColor WRITE setFirstColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor secondColor READ secondColor WRITE setSecondColor NOTIFY colorsChanged FINAL)
    Q_PROPERTY(QColor gridColor READ gridColor WRITE setGridColor NOTIFY colorsChanged FINAL)

    /// Why no trace is shown, or "live": a word for QML to put into words.
    Q_PROPERTY(QString feedState READ feedState NOTIFY liveChanged FINAL)
    /// Whether a trace is being drawn.
    Q_PROPERTY(bool showing READ isShowing NOTIFY liveChanged FINAL)
    /// With the trigger on: whether the last trace started on a crossing.
    Q_PROPERTY(bool crossed READ crossed NOTIFY readingsChanged FINAL)
    /// How many new sounds arrive a second at this cycle length, or nought.
    Q_PROPERTY(qreal arrivalsPerSecond READ arrivalsPerSecond NOTIFY readingsChanged FINAL)
    /// How many vertices the last picture took, and how many pictures have
    /// been worked out, for a test. **Not to be bound to**: a binding here
    /// would run at every frame of the screen.
    Q_PROPERTY(int vertexCount READ vertexCount FINAL)
    Q_PROPERTY(int pictures READ pictures FINAL)
    /// Whether it is being kept drawn at every frame of the screen.
    Q_PROPERTY(bool ticking READ isTicking NOTIFY liveChanged FINAL)
    /// Whether it holds a feed: is on screen, and is asking for sound.
    Q_PROPERTY(bool subscribed READ isSubscribed NOTIFY liveChanged FINAL)

public:
    /// A bar's pitch and a dot's, in columns.
    static constexpr int kBarPitch = 4;
    static constexpr int kDotPitch = 3;
    /// The most columns drawn, whatever the width.
    static constexpr int kMaxColumns = 4096;

    explicit ScopeItem(QQuickItem *parent = nullptr);
    ~ScopeItem() override;

    [[nodiscard]] ScopeHub *hub() const;
    void setHub(ScopeHub *hub);
    [[nodiscard]] QString blockId() const;
    void setBlockId(const QString &blockId);

    [[nodiscard]] qreal refreshHz() const;
    void setRefreshHz(qreal hz);
    [[nodiscard]] QString motion() const;
    void setMotion(const QString &motion);
    [[nodiscard]] QString style() const;
    void setStyle(const QString &style);
    [[nodiscard]] qreal spanMs() const;
    void setSpanMs(qreal ms);
    [[nodiscard]] qreal gainDb() const;
    void setGainDb(qreal db);
    [[nodiscard]] bool autoFit() const;
    void setAutoFit(bool on);
    [[nodiscard]] QString channels() const;
    void setChannels(const QString &channels);
    [[nodiscard]] bool trigger() const;
    void setTrigger(bool on);
    [[nodiscard]] bool hold() const;
    void setHold(bool on);
    [[nodiscard]] bool grid() const;
    void setGrid(bool on);
    [[nodiscard]] QString heightScale() const;
    void setHeightScale(const QString &scale);

    [[nodiscard]] QColor firstColor() const;
    void setFirstColor(const QColor &color);
    [[nodiscard]] QColor secondColor() const;
    void setSecondColor(const QColor &color);
    [[nodiscard]] QColor gridColor() const;
    void setGridColor(const QColor &color);

    [[nodiscard]] QString feedState() const;
    [[nodiscard]] bool isShowing() const;
    [[nodiscard]] bool crossed() const;
    [[nodiscard]] qreal arrivalsPerSecond() const;
    [[nodiscard]] int vertexCount() const;
    [[nodiscard]] int pictures() const;
    [[nodiscard]] bool isTicking() const;
    [[nodiscard]] bool isSubscribed() const;

    /// The heights of one trace now, for a test: [low, high, low, high, ...].
    [[nodiscard]] Q_INVOKABLE QVariantList heights(int trace) const;

    /// The picture as a test wants it: worked out now, without a frame.
    Q_INVOKABLE void work();

Q_SIGNALS:
    void sourceChanged();
    void settingsChanged();
    void colorsChanged();
    void liveChanged();
    /// Said only when one of them changes, and not with every picture.
    void readingsChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *node, UpdatePaintNodeData *data) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private:
    void changeSettings();
    /// Holds a feed, or lets it go, as what is on screen says.
    void follow();
    void adopt(ScopeFeed *feed);
    [[nodiscard]] bool isOnScreen() const;
    void followWindow(QQuickWindow *window);
    void letGoUnlessShown();
    void followFrames();
    void onRefreshed();
    void onStateChanged();
    void onFrameSwapped();
    void sayReadings();
    [[nodiscard]] int columnCount() const;

    QPointer<ScopeHub> m_hub;
    QPointer<ScopeFeed> m_feed;
    QString m_blockId;

    ScopeSettings m_settings;
    QString m_motion{QStringLiteral("realTime")};
    QString m_style{QStringLiteral("line")};
    QString m_channels{QStringLiteral("both")};
    QString m_scale{QStringLiteral("linear")};
    bool m_hold = false;
    bool m_grid = true;

    QColor m_firstColor{QStringLiteral("#3daee9")};
    QColor m_secondColor{QStringLiteral("#f5a623")};
    QColor m_gridColor{QStringLiteral("#31363b")};

    ScopeView m_view;
    int m_vertexCount = 0;
    int m_pictures = 0;
    bool m_ticking = false;
    bool m_lettingGo = false;
    bool m_saidCrossed = false;
    qreal m_saidArrivals = 0.0;
    QMetaObject::Connection m_windowShown;
    QMetaObject::Connection m_frames;
    QPointer<QQuickWindow> m_window;
};

} // namespace rb::ui

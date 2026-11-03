// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "scopehub.h"
#include "scopeview.h"

#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>
#include <QString>

namespace rb::ui
{

/// **What every item that draws a block's sound has to get right**, whatever
/// it draws: holding a feed, following its window, and asking for frames.
///
/// **It holds nothing about the window it is in.** A card moves between
/// windows as it goes outside the main one and back, and each window has its
/// own render loop, so the nodes belong to whichever window draws them and are
/// built from nothing if asked, and what ticks it is looked up again when the
/// window changes. Everything that lasts is in the feed and the view.
///
/// **Nothing ticks while nobody can see it.** It holds a feed only while
/// visible in a shown window, and that is what keeps the block's sound coming;
/// and it asks to be drawn at each frame of the screen only while Smooth is on
/// and there is something to move.
///
/// **The feed is let go of a moment later and not at once**, because a card
/// going from one window to another belongs to no window for an instant, and
/// the feed is where its sound is.
class ScopeSurface : public QQuickItem
{
    Q_OBJECT
    QML_ANONYMOUS

    /// Where the sound comes from: Meters.scopes.
    Q_PROPERTY(rb::ui::ScopeHub *hub READ hub WRITE setHub NOTIFY sourceChanged FINAL)
    Q_PROPERTY(QString blockId READ blockId WRITE setBlockId NOTIFY sourceChanged FINAL)

    /// The settings every such block has, as they are spelled in the session.
    Q_PROPERTY(qreal refreshHz READ refreshHz WRITE setRefreshHz NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString motion READ motion WRITE setMotion NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool hold READ hold WRITE setHold NOTIFY settingsChanged FINAL)

    /// Why nothing is shown, or "live": a word for QML to put into words.
    Q_PROPERTY(QString feedState READ feedState NOTIFY liveChanged FINAL)
    /// Whether the sound is being drawn.
    Q_PROPERTY(bool showing READ isShowing NOTIFY liveChanged FINAL)
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
    explicit ScopeSurface(QQuickItem *parent = nullptr);
    ~ScopeSurface() override;

    [[nodiscard]] ScopeHub *hub() const;
    void setHub(ScopeHub *hub);
    [[nodiscard]] QString blockId() const;
    void setBlockId(const QString &blockId);

    [[nodiscard]] qreal refreshHz() const;
    void setRefreshHz(qreal hz);
    [[nodiscard]] QString motion() const;
    void setMotion(const QString &motion);
    [[nodiscard]] bool hold() const;
    void setHold(bool on);

    [[nodiscard]] QString feedState() const;
    [[nodiscard]] bool isShowing() const;
    [[nodiscard]] qreal arrivalsPerSecond() const;
    [[nodiscard]] int vertexCount() const;
    [[nodiscard]] int pictures() const;
    [[nodiscard]] bool isTicking() const;
    [[nodiscard]] bool isSubscribed() const;

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
    /// What is drawn has been forgotten: the block or the feed is another.
    virtual void startAfresh() = 0;
    /// Works out the picture of now from the feed, which exists. Returns
    /// whether it changed.
    virtual bool workOutPicture() = 0;
    /// Says what the surface's own readings are, if they have changed.
    virtual void sayOwnReadings() = 0;

    /// A setting the feed or the frames depend on has changed.
    void changeSettings();
    [[nodiscard]] ScopeFeed *feed() const
    {
        return m_feed;
    }

    /// For updatePaintNode(): counts a picture, and that a frame was built of
    /// \a vertices vertices.
    void pictureWorkedOut(bool changed);
    void frameBuilt(int vertices);

    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

    ScopeSettings m_settings;

private:
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

    QPointer<ScopeHub> m_hub;
    QPointer<ScopeFeed> m_feed;
    QString m_blockId;
    QString m_motion{QStringLiteral("realTime")};
    bool m_hold = false;

    int m_vertexCount = 0;
    int m_pictures = 0;
    bool m_ticking = false;
    bool m_lettingGo = false;
    qreal m_saidArrivals = 0.0;
    QMetaObject::Connection m_windowShown;
    QMetaObject::Connection m_frames;
    QPointer<QQuickWindow> m_window;
};

} // namespace rb::ui

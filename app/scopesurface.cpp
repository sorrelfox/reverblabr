// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "scopesurface.h"

#include <QQuickWindow>

using namespace Qt::StringLiterals;

namespace rb::ui
{

ScopeSurface::ScopeSurface(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

ScopeSurface::~ScopeSurface()
{
    adopt(nullptr);
}

ScopeHub *ScopeSurface::hub() const
{
    return m_hub;
}

void ScopeSurface::setHub(ScopeHub *hub)
{
    if (hub != m_hub) {
        adopt(nullptr);
        m_hub = hub;
        Q_EMIT sourceChanged();
        follow();
    }
}

QString ScopeSurface::blockId() const
{
    return m_blockId;
}

void ScopeSurface::setBlockId(const QString &blockId)
{
    if (blockId != m_blockId) {
        adopt(nullptr);
        m_blockId = blockId;
        startAfresh();
        Q_EMIT sourceChanged();
        follow();
    }
}

void ScopeSurface::changeSettings()
{
    if (m_feed) {
        m_feed->setSettings(m_settings);
    }
    followFrames();
    update();
    Q_EMIT settingsChanged();
}

qreal ScopeSurface::refreshHz() const
{
    return m_settings.refreshHz;
}

void ScopeSurface::setRefreshHz(qreal hz)
{
    if (hz > 0.0 && hz != m_settings.refreshHz) {
        m_settings.refreshHz = hz;
        changeSettings();
    }
}

QString ScopeSurface::motion() const
{
    return m_motion;
}

void ScopeSurface::setMotion(const QString &motion)
{
    if (motion != m_motion) {
        m_motion = motion;
        m_settings.smooth = motion == "smooth"_L1;
        changeSettings();
    }
}

bool ScopeSurface::hold() const
{
    return m_hold;
}

void ScopeSurface::setHold(bool on)
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

QString ScopeSurface::feedState() const
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

bool ScopeSurface::isShowing() const
{
    return m_feed && m_feed->hasTrace();
}

qreal ScopeSurface::arrivalsPerSecond() const
{
    return m_feed ? m_feed->arrivalsPerSecond() : 0.0;
}

int ScopeSurface::vertexCount() const
{
    return m_vertexCount;
}

int ScopeSurface::pictures() const
{
    return m_pictures;
}

bool ScopeSurface::isTicking() const
{
    return m_ticking;
}

bool ScopeSurface::isSubscribed() const
{
    return !m_feed.isNull();
}

void ScopeSurface::work()
{
    if (m_feed) {
        m_feed->tick();
        pictureWorkedOut(workOutPicture());
        sayReadings();
    }
}

void ScopeSurface::pictureWorkedOut(bool changed)
{
    if (changed) {
        ++m_pictures;
    }
}

void ScopeSurface::frameBuilt(int vertices)
{
    m_vertexCount = vertices;
    // On this item's own thread, after the frame.
    QMetaObject::invokeMethod(this, &ScopeSurface::sayReadings, Qt::QueuedConnection);
}

void ScopeSurface::adopt(ScopeFeed *feed)
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
        connect(m_feed, &ScopeFeed::refreshed, this, &ScopeSurface::onRefreshed);
        connect(m_feed, &ScopeFeed::stateChanged, this, &ScopeSurface::onStateChanged);
        startAfresh();
    }
    followFrames();
    Q_EMIT liveChanged();
}

bool ScopeSurface::isOnScreen() const
{
    return isVisible() && window() != nullptr && window()->isVisible() && m_hub && !m_blockId.isEmpty();
}

void ScopeSurface::follow()
{
    if (isOnScreen()) {
        if (!m_feed) {
            adopt(m_hub->subscribe(m_blockId));
        }
    } else if (m_feed && !m_lettingGo) {
        m_lettingGo = true;
        QMetaObject::invokeMethod(this, &ScopeSurface::letGoUnlessShown, Qt::QueuedConnection);
    }
    update();
}

void ScopeSurface::letGoUnlessShown()
{
    m_lettingGo = false;
    if (!isOnScreen()) {
        adopt(nullptr);
    }
}

void ScopeSurface::followWindow(QQuickWindow *window)
{
    disconnect(m_windowShown);
    disconnect(m_frames);
    m_ticking = false;
    m_window = window;
    if (window != nullptr) {
        m_windowShown = connect(window, &QWindow::visibleChanged, this, &ScopeSurface::follow);
    }
    follow();
    followFrames();
}

void ScopeSurface::followFrames()
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
            connect(m_window, &QQuickWindow::frameSwapped, this, &ScopeSurface::onFrameSwapped, Qt::QueuedConnection);
        update();
    } else {
        disconnect(m_frames);
    }
    Q_EMIT liveChanged();
}

void ScopeSurface::onRefreshed()
{
    followFrames();
    update();
}

void ScopeSurface::onStateChanged()
{
    followFrames();
    update();
    Q_EMIT liveChanged();
}

void ScopeSurface::sayReadings()
{
    sayOwnReadings();
    const qreal arrivals = arrivalsPerSecond();
    if (arrivals != m_saidArrivals) {
        m_saidArrivals = arrivals;
        Q_EMIT readingsChanged();
    }
}

void ScopeSurface::onFrameSwapped()
{
    if (m_ticking) {
        update();
    }
}

void ScopeSurface::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

void ScopeSurface::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange) {
        followWindow(value.window);
    } else if (change == ItemVisibleHasChanged) {
        follow();
    }
}

} // namespace rb::ui

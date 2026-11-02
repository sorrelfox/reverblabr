// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "windowwatch.h"

#include <QEvent>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QRegion>
#include <QVariant>

#include <cmath>

namespace rb::ui
{

using namespace Qt::StringLiterals;

namespace
{

/// Popup.Item and Popup.Window, the values of the `popupType` Qt Quick
/// Controls gives every popup. **The enum is private to it**, and these two
/// are in its documentation.
constexpr int kItemPopup = 0;
constexpr int kWindowPopup = 1;

/// Marks a popup this class changed, for `releasePopups()`.
constexpr const char *kChangedHere = "_rb_popupTypeChanged";

} // namespace

WindowWatch::WindowWatch(QObject *parent)
    : QObject(parent)
{
}

QWindow *WindowWatch::window() const
{
    return m_window;
}

void WindowWatch::setWindow(QWindow *window)
{
    if (m_window == window) {
        return;
    }
    if (m_window) {
        m_window->removeEventFilter(this);
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
        connect(m_window, &QWindow::visibleChanged, this, &WindowWatch::noteShowing);
    }
    Q_EMIT windowChanged();
    noteShowing();
    applyInputArea();
}

bool WindowWatch::showing() const
{
    return m_showing;
}

void WindowWatch::noteShowing()
{
    const bool showing = m_window && m_window->isVisible() && m_window->isExposed();
    if (m_showing != showing) {
        m_showing = showing;
        Q_EMIT showingChanged();
    }
}

bool WindowWatch::followsPointer() const
{
    return m_followsPointer;
}

void WindowWatch::setFollowsPointer(bool follows)
{
    if (m_followsPointer != follows) {
        m_followsPointer = follows;
        Q_EMIT followsPointerChanged();
    }
}

QQuickItem *WindowWatch::panel() const
{
    return m_panel;
}

void WindowWatch::setPanel(QQuickItem *panel)
{
    if (m_panel != panel) {
        m_panel = panel;
        Q_EMIT panelChanged();
        preparePopups();
    }
}

QQuickItem *WindowWatch::dialogOverlay() const
{
    return m_dialogOverlay;
}

void WindowWatch::setDialogOverlay(QQuickItem *overlay)
{
    if (m_dialogOverlay != overlay) {
        m_dialogOverlay = overlay;
        Q_EMIT dialogOverlayChanged();
        preparePopups();
    }
}

QRectF WindowWatch::inputArea() const
{
    return m_inputArea;
}

void WindowWatch::setInputArea(const QRectF &area)
{
    if (m_inputArea != area) {
        m_inputArea = area;
        Q_EMIT inputAreaChanged();
        applyInputArea();
    }
}

void WindowWatch::applyInputArea()
{
    // **Wayland only.** There a window's mask is where it takes input. On X11
    // it is also where it is drawn, and would cut the shadow off.
    if (m_window && !m_inputArea.isEmpty() && QGuiApplication::platformName() == "wayland"_L1) {
        m_window->setMask(QRegion(m_inputArea.toAlignedRect()));
    }
}

void WindowWatch::preparePopups()
{
    if (!m_panel) {
        return;
    }
    const QList<QObject *> objects = m_panel->findChildren<QObject *>();
    for (QObject *object : objects) {
        if (object->inherits("QQuickDialog")) {
            if (m_dialogOverlay && !object->property("visible").toBool()) {
                object->setProperty("parent", QVariant::fromValue(m_dialogOverlay.data()));
            }
        } else if (object->inherits("QQuickPopup") && !object->inherits("QQuickToolTip")
                   && object->property("popupType").toInt() == kItemPopup) {
            object->setProperty("popupType", kWindowPopup);
            object->setProperty(kChangedHere, true);
        }
    }
}

void WindowWatch::releasePopups()
{
    if (!m_panel) {
        return;
    }
    const QList<QObject *> objects = m_panel->findChildren<QObject *>();
    for (QObject *object : objects) {
        if (object->property(kChangedHere).toBool()) {
            object->setProperty("popupType", kItemPopup);
            object->setProperty(kChangedHere, QVariant{});
        }
    }
}

bool WindowWatch::focusWentToOwnPopup() const
{
    for (const QWindow *focus = QGuiApplication::focusWindow(); focus != nullptr; focus = focus->transientParent()) {
        if (focus->transientParent() == m_window) {
            return true;
        }
    }
    return false;
}

bool WindowWatch::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window) {
        return QObject::eventFilter(watched, event);
    }
    switch (event->type()) {
    case QEvent::Expose:
    case QEvent::Show:
    case QEvent::Hide:
        noteShowing();
        break;
    case QEvent::MouseMove:
        if (m_followsPointer) {
            Q_EMIT pointerMoved(static_cast<QMouseEvent *>(event)->position());
        }
        break;
    case QEvent::MouseButtonRelease:
        if (m_followsPointer) {
            Q_EMIT pointerReleased();
        }
        break;
    case QEvent::MouseButtonPress:
    case QEvent::KeyPress:
        if (m_panel) {
            preparePopups();
        }
        break;
    case QEvent::FocusOut:
        // **A popup in a window of its own takes the keyboard on Wayland**,
        // and a combo box whose window loses it closes its popup: it would
        // shut as it opened. The popup is part of this window's business, so
        // the window is not told.
        if (m_panel && focusWentToOwnPopup()) {
            return true;
        }
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

} // namespace rb::ui

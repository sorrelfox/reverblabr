// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRectF>
#include <QWindow>

namespace rb::ui
{

/// **What QML cannot ask of a window**, for the settings panels that leave
/// the main window (ADR-0032). One of these watches one window, and each thing
/// it does is off until it is asked for.
///
/// For the main window: whether it can be seen, and the pointer while a panel
/// is carried out of it, which the window alone is told about. For a panel's
/// own window: its popups, its focus and the part of it that takes the pointer.
class WindowWatch : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The window watched.
    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    /// Whether it is up and the desktop is showing it. **Not `visibility`**:
    /// on Wayland a minimised window says it is windowed, and only stops being
    /// exposed.
    Q_PROPERTY(bool showing READ showing NOTIFY showingChanged)
    /// Reports the pointer from raw events, with `pointerMoved` and
    /// `pointerReleased`. **A press keeps the pointer on this window** however
    /// far it goes, even once the item that took the press has left it.
    Q_PROPERTY(bool followsPointer READ followsPointer WRITE setFollowsPointer NOTIFY followsPointerChanged)
    /// The panel in this window, whose popups are made ready for it before
    /// each press and key (`preparePopups()`). A card-sized window would clip
    /// a popup drawn inside it.
    Q_PROPERTY(QQuickItem *panel READ panel WRITE setPanel NOTIFY panelChanged)
    /// Where the panel's dialogs open: the main window's overlay, where they
    /// open for a panel inside it.
    Q_PROPERTY(QQuickItem *dialogOverlay READ dialogOverlay WRITE setDialogOverlay NOTIFY dialogOverlayChanged)
    /// The part of the window that takes the pointer. The rest is the card's
    /// shadow, and a click there belongs to what is underneath. Empty is all
    /// of it.
    Q_PROPERTY(QRectF inputArea READ inputArea WRITE setInputArea NOTIFY inputAreaChanged)

public:
    explicit WindowWatch(QObject *parent = nullptr);

    [[nodiscard]] QWindow *window() const;
    void setWindow(QWindow *window);
    [[nodiscard]] bool showing() const;
    [[nodiscard]] bool followsPointer() const;
    void setFollowsPointer(bool follows);
    [[nodiscard]] QQuickItem *panel() const;
    void setPanel(QQuickItem *panel);
    [[nodiscard]] QQuickItem *dialogOverlay() const;
    void setDialogOverlay(QQuickItem *overlay);
    [[nodiscard]] QRectF inputArea() const;
    void setInputArea(const QRectF &area);

    /// Sends the panel's dialogs to `dialogOverlay` and makes its other
    /// popups windows of their own. **Asked again before each press and key**,
    /// as a panel's rows, and so their popups, come and go with its block.
    Q_INVOKABLE void preparePopups();
    /// Puts back the popups `preparePopups()` changed, for a panel going home.
    Q_INVOKABLE void releasePopups();

Q_SIGNALS:
    void windowChanged();
    void showingChanged();
    void followsPointerChanged();
    void panelChanged();
    void dialogOverlayChanged();
    void inputAreaChanged();
    /// The pointer is at \a position in the window, which may be outside it.
    void pointerMoved(QPointF position);
    void pointerReleased();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void noteShowing();
    void applyInputArea();
    /// Whether the keyboard has only gone to a popup of this window.
    [[nodiscard]] bool focusWentToOwnPopup() const;

    QPointer<QWindow> m_window;
    QPointer<QQuickItem> m_panel;
    QPointer<QQuickItem> m_dialogOverlay;
    QRectF m_inputArea;
    bool m_showing = false;
    bool m_followsPointer = false;
};

} // namespace rb::ui

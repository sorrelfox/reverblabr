// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QImage>
#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickWindow>

#include <memory>

class KWindowShadow;

namespace rb::ui
{

/// What a decoration does for a window that has none (ADR-0012).
///
/// In the Reverblabr appearances the window is frameless and its top row is
/// the design's header, so the two things a title bar gets from the
/// compositor for free have to be asked for:
///
/// - **The shadow**, drawn by the compositor through KWindowShadow rather
///   than by us inside a translucent margin. A margin of our own would be
///   part of the window, so it would take the clicks meant for whatever is
///   under it and move and snap with the wrong edges.
/// - **The window menu** — move, resize, keep above, the virtual desktops,
///   close — which only the compositor can offer, because only it can keep a
///   window above the others or send it to another desktop.
class WindowFrame : public QObject
{
    Q_OBJECT
    QML_ELEMENT

    /// The window the frame belongs to.
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    /// Whether the compositor draws the design's shadow around it: not while
    /// it is maximised, where the design has none.
    Q_PROPERTY(bool shadowed READ shadowed WRITE setShadowed NOTIFY shadowedChanged)
    /// The window's corner radius, which the shadow's corners follow.
    Q_PROPERTY(int radius READ radius WRITE setRadius NOTIFY radiusChanged)
    /// Whether the compositor has the shadow: false offscreen and on a
    /// platform with no shadow protocol, where there is none to draw.
    Q_PROPERTY(bool shadowInstalled READ shadowInstalled NOTIFY shadowInstalledChanged)

public:
    explicit WindowFrame(QObject *parent = nullptr);
    ~WindowFrame() override;

    [[nodiscard]] QQuickWindow *window() const;
    void setWindow(QQuickWindow *window);
    [[nodiscard]] bool shadowed() const;
    void setShadowed(bool shadowed);
    [[nodiscard]] int radius() const;
    void setRadius(int radius);
    [[nodiscard]] bool shadowInstalled() const;

    /// Asks the compositor to show its own menu for the window, at the
    /// pointer. False when it cannot be asked — a platform other than
    /// Wayland, or a build without Qt's Wayland client internals — and the
    /// caller shows a menu of its own instead.
    Q_INVOKABLE bool showWindowMenu();

    /// How far the design's shadow reaches past each edge of the window, in
    /// logical pixels: CSS's `0 24px 64px`, a blur of 64 pulled down by 24.
    [[nodiscard]] static QMargins shadowExtents();

    /// The shadow as one image: the window as a rounded box of
    /// \a radius at (left, top) of the extents, its shadow blurred around
    /// it, and the box itself cut out. The tiles are cut from this.
    [[nodiscard]] static QImage shadowImage(int radius);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void windowChanged();
    void shadowedChanged();
    void radiusChanged();
    void shadowInstalledChanged();

private:
    void updateShadow();
    void setShadowInstalled(bool installed);

    QPointer<QQuickWindow> m_window;
    std::unique_ptr<KWindowShadow> m_shadow;
    bool m_shadowed = false;
    bool m_shadowInstalled = false;
    int m_radius = 0;
    int m_shadowRadius = -1;
};

} // namespace rb::ui

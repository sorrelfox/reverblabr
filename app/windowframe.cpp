// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "windowframe.h"

#include <KWindowShadow>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPainter>
#include <QPlatformSurfaceEvent>

#include <algorithm>
#include <cmath>
#include <vector>

#if REVERBLABR_WAYLAND_WINDOW_MENU
#include <QtWaylandClient/private/qwaylanddisplay_p.h>
#include <QtWaylandClient/private/qwaylandshellsurface_p.h>
#include <QtWaylandClient/private/qwaylandwindow_p.h>
#endif

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

Q_LOGGING_CATEGORY(lcWindowFrame, "reverblabr.windowframe")

// The design's shadow: `0 24px 64px rgba(0,0,0,.45)`. CSS draws a blur of
// 64 as a Gaussian of half that, which is all but gone 64 pixels out.
constexpr int kBlur = 64;
constexpr int kOffsetY = 24;
constexpr qreal kAlpha = 0.45;

// **The box the shadow is cast by is big**, not a corner's worth: a blur
// this wide over a small box dilutes it, and an edge tile is a slice of what
// a long straight edge casts. Four deviations either side of the middle.
constexpr int kBox = 4 * kBlur + 1;

/// One pass of a running-sum box blur along each row of \a alpha.
void boxBlurRows(std::vector<float> &alpha, int width, int height, int reach)
{
    std::vector<float> row(static_cast<size_t>(width));
    const float scale = 1.0F / static_cast<float>(2 * reach + 1);
    for (int y = 0; y < height; ++y) {
        float *line = alpha.data() + static_cast<ptrdiff_t>(y) * width;
        std::copy(line, line + width, row.begin());
        float sum = 0.0F;
        const auto at = [&row, width](int x) { return x < 0 || x >= width ? 0.0F : row[static_cast<size_t>(x)]; };
        for (int x = -reach; x <= reach; ++x) {
            sum += at(x);
        }
        for (int x = 0; x < width; ++x) {
            line[x] = sum * scale;
            sum += at(x + reach + 1) - at(x - reach);
        }
    }
}

/// The same down each column, by way of a transpose.
void boxBlurColumns(std::vector<float> &alpha, int width, int height, int reach)
{
    std::vector<float> transposed(alpha.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            transposed[static_cast<size_t>(x) * height + y] = alpha[static_cast<size_t>(y) * width + x];
        }
    }
    boxBlurRows(transposed, height, width, reach);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            alpha[static_cast<size_t>(y) * width + x] = transposed[static_cast<size_t>(x) * height + y];
        }
    }
}

KWindowShadowTile::Ptr tileOf(const QImage &image, const QRect &rect)
{
    auto tile = KWindowShadowTile::Ptr::create();
    tile->setImage(image.copy(rect));
    return tile;
}

} // namespace

WindowFrame::WindowFrame(QObject *parent)
    : QObject(parent)
{
}

WindowFrame::~WindowFrame() = default;

QQuickWindow *WindowFrame::window() const
{
    return m_window;
}

void WindowFrame::setWindow(QQuickWindow *window)
{
    if (m_window == window) {
        return;
    }
    if (m_window) {
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_shadow.reset();
    setShadowInstalled(false);
    if (m_window) {
        m_window->removeEventFilter(this);
    }
    m_window = window;
    if (m_window) {
        // **The shadow belongs to the surface**, which is made after QML has
        // handed the window over, and made again after the window has been
        // hidden: so it is watched for rather than assumed.
        m_window->installEventFilter(this);
        connect(m_window, &QWindow::visibleChanged, this, &WindowFrame::updateShadow);
    }
    Q_EMIT windowChanged();
    updateShadow();
}

bool WindowFrame::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_window && event->type() == QEvent::PlatformSurface) {
        const auto *surface = static_cast<QPlatformSurfaceEvent *>(event);
        if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
            // After the event, when the platform window is all there.
            QMetaObject::invokeMethod(this, &WindowFrame::updateShadow, Qt::QueuedConnection);
        } else if (m_shadow) {
            m_shadow->destroy();
            setShadowInstalled(false);
        }
    }
    return QObject::eventFilter(watched, event);
}

bool WindowFrame::shadowed() const
{
    return m_shadowed;
}

void WindowFrame::setShadowed(bool shadowed)
{
    if (m_shadowed == shadowed) {
        return;
    }
    m_shadowed = shadowed;
    Q_EMIT shadowedChanged();
    updateShadow();
}

int WindowFrame::radius() const
{
    return m_radius;
}

void WindowFrame::setRadius(int radius)
{
    if (m_radius == radius) {
        return;
    }
    m_radius = radius;
    Q_EMIT radiusChanged();
    updateShadow();
}

bool WindowFrame::shadowInstalled() const
{
    return m_shadowInstalled;
}

void WindowFrame::setShadowInstalled(bool installed)
{
    if (m_shadowInstalled == installed) {
        return;
    }
    qCDebug(lcWindowFrame) << (installed ? "the compositor has the shadow" : "no shadow");
    m_shadowInstalled = installed;
    Q_EMIT shadowInstalledChanged();
}

QMargins WindowFrame::shadowExtents()
{
    return {kBlur, kBlur - kOffsetY, kBlur, kBlur + kOffsetY};
}

QImage WindowFrame::shadowImage(int radius)
{
    const QMargins extents = shadowExtents();
    const int width = extents.left() + kBox + extents.right();
    const int height = extents.top() + kBox + extents.bottom();
    const QRectF window(extents.left(), extents.top(), kBox, kBox);

    QImage box(width, height, QImage::Format_ARGB32_Premultiplied);
    box.fill(Qt::transparent);
    {
        QPainter painter(&box);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawRoundedRect(window.translated(0, kOffsetY), radius, radius);
    }

    // **Three box blurs make a Gaussian** to well within what anybody sees,
    // for a fraction of the work: each pass is a running sum. The widths are
    // the ones whose three passes have the deviation CSS asks for.
    std::vector<float> alpha(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(box.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            alpha[static_cast<size_t>(y) * width + x] = static_cast<float>(qAlpha(line[x])) / 255.0F;
        }
    }
    const double deviation = kBlur / 2.0;
    const int reach =
        std::max(1, static_cast<int>(std::lround((std::sqrt(4.0 * deviation * deviation + 1.0) - 1.0) / 2.0)));
    for (int pass = 0; pass < 3; ++pass) {
        boxBlurRows(alpha, width, height, reach);
        boxBlurColumns(alpha, width, height, reach);
    }

    QImage shadow(width, height, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(shadow.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const int a = std::clamp(
                static_cast<int>(std::lround(alpha[static_cast<size_t>(y) * width + x] * kAlpha * 255.0)), 0, 255);
            line[x] = qRgba(0, 0, 0, a);
        }
    }
    // **Nothing under the window itself**: where the window is, it hides
    // the shadow, except in its rounded corners, which must show the
    // shadow round them rather than a square of it.
    {
        QPainter painter(&shadow);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawRoundedRect(window, radius, radius);
    }
    return shadow;
}

void WindowFrame::updateShadow()
{
    // **Only where a compositor can be handed one**: offscreen, or on a
    // platform KWindowSystem has no plugin for, there is nobody to draw it.
    const QString platform = QGuiApplication::platformName();
    const bool compositor = platform.startsWith(u"wayland"_s) || platform == u"xcb"_s;
    const bool wanted = compositor && m_window && m_shadowed && m_window->isVisible() && m_window->handle() != nullptr;
    qCDebug(lcWindowFrame) << "shadow wanted" << wanted << "on" << platform << "shadowed" << m_shadowed << "visible"
                           << (m_window && m_window->isVisible()) << "surface" << (m_window && m_window->handle());
    if (!wanted) {
        if (m_shadow) {
            m_shadow->destroy();
        }
        setShadowInstalled(false);
        return;
    }
    if (m_shadow && m_shadowRadius == m_radius) {
        if (!m_shadow->isCreated()) {
            setShadowInstalled(m_shadow->create());
        }
        return;
    }

    // **Corner tiles reach in by the radius**, so the rounded corner the
    // window leaves transparent shows the shadow rather than the desktop.
    const QImage image = shadowImage(m_radius);
    const QMargins e = shadowExtents();
    const int r = m_radius;
    const int middleX = e.left() + kBox / 2;
    const int middleY = e.top() + kBox / 2;
    const int right = e.left() + kBox;
    const int bottom = e.top() + kBox;

    m_shadow = std::make_unique<KWindowShadow>();
    m_shadowRadius = m_radius;
    m_shadow->setTopLeftTile(tileOf(image, QRect(0, 0, e.left() + r, e.top() + r)));
    m_shadow->setTopTile(tileOf(image, QRect(middleX, 0, 1, e.top())));
    m_shadow->setTopRightTile(tileOf(image, QRect(right - r, 0, r + e.right(), e.top() + r)));
    m_shadow->setLeftTile(tileOf(image, QRect(0, middleY, e.left(), 1)));
    m_shadow->setRightTile(tileOf(image, QRect(right, middleY, e.right(), 1)));
    m_shadow->setBottomLeftTile(tileOf(image, QRect(0, bottom - r, e.left() + r, r + e.bottom())));
    m_shadow->setBottomTile(tileOf(image, QRect(middleX, bottom, 1, e.bottom())));
    m_shadow->setBottomRightTile(tileOf(image, QRect(right - r, bottom - r, r + e.right(), r + e.bottom())));
    m_shadow->setPadding(e);
    m_shadow->setWindow(m_window);
    setShadowInstalled(m_shadow->create());
}

bool WindowFrame::showWindowMenu()
{
    if (!m_window || !m_window->handle()) {
        return false;
    }
#if REVERBLABR_WAYLAND_WINDOW_MENU
    // **Qt's own client internals**, the one way to ask for it: Qt has no
    // public call for the window menu, and its own client-side decoration
    // opens it exactly like this. Private, so it is built only where the
    // headers are, and checked against the Qt it was built with.
    if (QGuiApplication::platformName().startsWith(u"wayland"_s)) {
        auto *platformWindow = static_cast<QtWaylandClient::QWaylandWindow *>(m_window->handle());
        QtWaylandClient::QWaylandShellSurface *surface = platformWindow->shellSurface();
        QtWaylandClient::QWaylandInputDevice *seat = platformWindow->display()->lastInputDevice();
        if (seat == nullptr) {
            seat = platformWindow->display()->defaultInputDevice();
        }
        return surface != nullptr && seat != nullptr && surface->showWindowMenu(seat);
    }
#endif
    return false;
}

} // namespace rb::ui

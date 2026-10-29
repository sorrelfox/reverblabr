// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "canvasgrid.h"

#include <QImage>
#include <QPainter>
#include <QQuickImageProvider>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// The name the url is addressed by: `image://reverblabr-grid/…`.
[[nodiscard]] QString providerName()
{
    return u"reverblabr-grid"_s;
}

/// A cell smaller than this has dots closer together than they can be told
/// apart, and one larger is a grid nobody can line anything up against.
constexpr qreal kSmallestCell = 6.0;
constexpr qreal kLargestCell = 128.0;
/// How big the dot is, in logical pixels at 100 %. A whole pixel radius,
/// so that the dot is a dot and not a smudge on a screen that is not
/// scaled.
constexpr qreal kDotRadius = 1.0;

class GridImageProvider : public QQuickImageProvider
{
public:
    GridImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    /// \a id is "<cell>/<ratio>/<aarrggbb>", which CanvasGrid::tile() wrote.
    /// **The colour has no `#` in front of it**: a hash in a url is the
    /// start of a fragment, so the provider would be handed a path with
    /// the colour cut off it.
    QImage requestImage(const QString &id, QSize *size, const QSize &) override
    {
        const QStringList parts = id.split(u'/');
        const qreal cell = qBound(kSmallestCell, parts.value(0).toDouble(), kLargestCell);
        const qreal ratio = qBound(0.5, parts.value(1).toDouble(), 8.0);
        QColor colour(u'#' + parts.value(2));
        if (!colour.isValid()) {
            colour = QColor(Qt::gray);
        }

        // **Whole device pixels.** A tile whose edge falls between two
        // pixels is a tile whose seams show once it is repeated a hundred
        // times across the canvas.
        const int pixels = qMax(1, qRound(cell * ratio));
        QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colour);
        const qreal radius = kDotRadius * ratio;
        painter.drawEllipse(QPointF(radius, radius), radius, radius);
        painter.end();

        image.setDevicePixelRatio(ratio);
        if (size != nullptr) {
            *size = QSize(pixels, pixels);
        }
        return image;
    }
};

/// Makes sure \a engine knows where the tiles come from.
///
/// **Asked every time rather than once at registration.** How a QML
/// singleton gets built is Qt's business and not something to depend on —
/// SessionModel makes the same point about where it gets its engine — and a
/// grid that silently failed to load would be a canvas with no grid and
/// nothing saying why. It costs a hash lookup, and a url is handed out when
/// the zoom or the colour scheme changes rather than once a frame.
void ensureProvider(QQmlEngine *engine)
{
    if (engine != nullptr && engine->imageProvider(providerName()) == nullptr) {
        engine->addImageProvider(providerName(), new GridImageProvider);
    }
}

} // namespace

CanvasGrid::CanvasGrid(QObject *parent)
    : QObject(parent)
{
}

CanvasGrid *CanvasGrid::create(QQmlEngine *engine, QJSEngine *)
{
    ensureProvider(engine);
    return new CanvasGrid;
}

QUrl CanvasGrid::tile(qreal cellPixels, const QColor &colour, qreal devicePixelRatio) const
{
    ensureProvider(qmlEngine(this));
    const qreal cell = qBound(kSmallestCell, cellPixels, kLargestCell);
    const qreal ratio = qBound(0.5, devicePixelRatio, 8.0);
    return QUrl(u"image://%1/%2/%3/%4"_s.arg(providerName(),
                                             QString::number(cell, 'f', 2),
                                             QString::number(ratio, 'f', 2),
                                             colour.name(QColor::HexArgb).sliced(1)));
}

} // namespace rb::ui

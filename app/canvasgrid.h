// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QColor>
#include <QObject>
#include <QQmlEngine>
#include <QUrl>

namespace rb::ui
{

/// One cell of the canvas's dot grid, painted to be repeated.
///
/// **A tiled image, by ADR-0009.** The spike measured a full-screen shader
/// and a tiled image at the same cost, and the image needs no shader
/// tooling, no compiled `.qsb` in the build and nothing to go wrong on a
/// driver. The cell is painted at the device pixel ratio it is asked for,
/// which is what keeps the dots round rather than square at 200 % and at
/// the canvas's own zoom.
///
/// **The colour is an argument, not a constant.** The grid is a colour-scheme
/// role (AppColors.grid), so the tile has to be repainted when the scheme
/// changes; putting the colour in the url is what makes that happen by
/// itself, because a new url is a new image.
class CanvasGrid : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    /// How far apart the dots are at 100 %, in logical pixels. Three times
    /// the eight-pixel snap, so that a tile dropped on the grid lands on a
    /// dot or halfway between two rather than anywhere at all.
    static constexpr int kCellPixels = 24;

    explicit CanvasGrid(QObject *parent = nullptr);

    /// Installs the image provider on \a engine and answers the singleton.
    static CanvasGrid *create(QQmlEngine *engine, QJSEngine *scriptEngine);

    /// The url of a tile \a cellPixels across, drawn in \a colour for a
    /// screen of \a devicePixelRatio. Sizes outside what a canvas can zoom
    /// to are clamped rather than refused, because an image that failed to
    /// load is a canvas with no grid and no way to tell why.
    [[nodiscard]] Q_INVOKABLE QUrl tile(qreal cellPixels, const QColor &colour, qreal devicePixelRatio) const;
};

} // namespace rb::ui

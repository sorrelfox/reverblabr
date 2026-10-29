// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The shadow the compositor draws round the frameless window: the
// design's `0 24px 64px rgba(0,0,0,.45)`, cut into the tiles KWindowShadow
// takes. Whether the compositor draws it is for a person to look at; what
// it is handed is measured here.

#include "windowframe.h"

#include <gtest/gtest.h>

using namespace rb;

namespace
{

int alphaAt(const QImage &image, int x, int y)
{
    return qAlpha(image.pixel(x, y));
}

} // namespace

TEST(WindowFrameTest, TheShadowReachesAsFarAsTheDesignsAndFurtherDownThanUp)
{
    // A blur of 64 pulled down by 24: 64 to each side, 40 above, 88 below.
    const QMargins extents = ui::WindowFrame::shadowExtents();
    EXPECT_EQ(extents.left(), 64);
    EXPECT_EQ(extents.right(), 64);
    EXPECT_EQ(extents.top(), 40);
    EXPECT_EQ(extents.bottom(), 88);
}

TEST(WindowFrameTest, TheShadowIsDarkestAtTheWindowAndGoneAtItsReach)
{
    const QImage image = ui::WindowFrame::shadowImage(8);
    const QMargins e = ui::WindowFrame::shadowExtents();
    const int middleX = image.width() / 2;
    const int middleY = image.height() / 2;

    // **Never darker than the design's 0.45.**
    int darkest = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            darkest = std::max(darkest, alphaAt(image, x, y));
        }
    }
    EXPECT_LE(darkest, 115) << "darker than rgba(0,0,0,.45)";
    EXPECT_GT(darkest, 60) << "the shadow is too faint to see";

    // **Heavier below than above**: the light is overhead, as the prototype
    // draws it. One pixel off each edge of the window.
    const int above = alphaAt(image, middleX, e.top() - 1);
    const int below = alphaAt(image, middleX, image.height() - e.bottom());
    EXPECT_GT(below, above);

    // **Fading all the way out**, so the tiles do not end in a hard line.
    EXPECT_LE(alphaAt(image, middleX, 0), 3);
    EXPECT_LE(alphaAt(image, 0, middleY), 3);
    EXPECT_LE(alphaAt(image, middleX, image.height() - 1), 3);
}

TEST(WindowFrameTest, NothingIsDrawnUnderTheWindowButItsCornersAreShadowed)
{
    // Where the window is, it covers its own shadow; a shadow under it would
    // show through the rounded corners the window leaves transparent.
    const int radius = 8;
    const QImage image = ui::WindowFrame::shadowImage(radius);
    const QMargins e = ui::WindowFrame::shadowExtents();
    EXPECT_EQ(alphaAt(image, image.width() / 2, image.height() / 2), 0);
    EXPECT_EQ(alphaAt(image, e.left() + radius, e.top() + radius), 0);

    // The very corner pixel of the window's square is outside its rounded
    // shape, so it shows the shadow like the rest of the outside.
    const int bottomLeftX = e.left();
    const int bottomLeftY = image.height() - e.bottom() - 1;
    EXPECT_GT(alphaAt(image, bottomLeftX, bottomLeftY), 0);
}

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami

// The type the interface is set in ([ADR-0012]).
//
// **The prototype's scale, in its own units.** It sets text in Noto Sans
// and numbers in JetBrains Mono, both bundled with the application, at
// sizes in CSS pixels with halves among them; `Appearance.points()` turns a
// pixel size into the point size Qt lays out at the same height. Each
// named font below is one of the prototype's uses, so a label asks for "a
// tile's title" rather than for 11 pixels.
//
// **In *Follow system* every name is what that label used before the
// application had a look of its own**: the desktop's font, small font and
// fixed-width font, scaled the way it was. Nothing outside this file names a
// size or a family.
QtObject {
    id: fonts

    readonly property bool followsSystem: Appearance.followsSystem

    // **The prototype's scale**, largest first, in CSS pixels. Held to the
    // prototype by tst_appfonts.qml.
    /// The running time in the header.
    readonly property real timerPixels: 17
    /// The number on a tile.
    readonly property real readoutPixels: 14
    /// A message in the middle of an empty canvas.
    readonly property real largePixels: 13
    /// Everything not otherwise named: labels, buttons, fields, tabs.
    readonly property real bodyPixels: 12.5
    /// A row of choices, the session's details.
    readonly property real compactPixels: 12
    /// A line explaining the one above it.
    readonly property real captionPixels: 11.5
    /// A section's heading, a tile's title, a small readout.
    readonly property real headingPixels: 11
    /// A library row's description.
    readonly property real detailPixels: 10.5
    /// A key cap, a line under a tile's picture.
    readonly property real tinyPixels: 10
    /// A meter's scale, the line under a tile's number.
    readonly property real microPixels: 9.5

    // The weights the prototype uses, and no others.
    readonly property int medium: Font.Medium
    readonly property int demiBold: Font.DemiBold
    readonly property int bold: Font.Bold

    /// A section heading's spacing between letters: 0.06 em.
    readonly property real headingLetterSpacing: fonts.headingPixels * 0.06

    /// The faces.
    readonly property string sansFamily: fonts.followsSystem ? Kirigami.Theme.defaultFont.family
                                                             : Appearance.sansFamily
    readonly property string monoFamily: fonts.followsSystem ? Kirigami.Theme.fixedWidthFont.family
                                                             : Appearance.monoFamily

    /// A font in one of the bundled faces at a size on the prototype's
    /// scale.
    function designed(family: string, pixels: real, weight: int): font {
        return fonts.sized(Qt.font({
                                       "family": family,
                                       "weight": weight
                                   }), Appearance.points(pixels));
    }

    /// \a font at \a points. **Set on the value, not handed to Qt.font()**,
    /// which keeps only whole points: 12.5 pixels is 9.375 of them.
    function sized(font: font, points: real): font {
        const sized = font;
        sized.pointSize = points;
        return sized;
    }

    /// The body text. Also the application's own font in the Reverblabr
    /// appearance, which is what every control not named here is set in.
    readonly property font body: fonts.followsSystem
                                 ? Kirigami.Theme.defaultFont
                                 : fonts.designed(Appearance.sansFamily, fonts.bodyPixels, Font.Normal)
    /// Body text that is pressed or chosen: the Run button, a library tab
    /// or a session tab in front.
    readonly property font strong: fonts.followsSystem
                                   ? fonts.sized(Qt.font({
                                                             "family": Kirigami.Theme.defaultFont.family,
                                                             "weight": Font.DemiBold
                                                         }), Kirigami.Theme.defaultFont.pointSize)
                                   : fonts.designed(Appearance.sansFamily, fonts.bodyPixels, Font.DemiBold)
    /// A button's label, and a choice in a segmented row: a little
    /// smaller than the body and heavier, so that it reads as something to
    /// press.
    readonly property font button: fonts.followsSystem
                                   ? Kirigami.Theme.defaultFont
                                   : fonts.designed(Appearance.sansFamily, fonts.compactPixels, Font.DemiBold)
    /// Secondary text: a caption, a hint, a description.
    readonly property font small: fonts.followsSystem
                                  ? Kirigami.Theme.smallFont
                                  : fonts.designed(Appearance.sansFamily, fonts.captionPixels, Font.Normal)
    /// A number that is read rather than glanced at: a parameter's value,
    /// a sample rate, a zoom level.
    readonly property font mono: fonts.followsSystem
                                 ? Kirigami.Theme.fixedWidthFont
                                 : fonts.designed(Appearance.monoFamily, fonts.headingPixels, Font.DemiBold)
    /// A section's heading, in capitals.
    readonly property font heading: fonts.followsSystem
                                    ? fonts.sized(Qt.font({
                                                              "family": Kirigami.Theme.smallFont.family,
                                                              "weight": Font.Bold
                                                          }), Kirigami.Theme.smallFont.pointSize)
                                    : fonts.sized(Qt.font({
                                                              "family": Appearance.sansFamily,
                                                              "weight": Font.Bold,
                                                              "letterSpacing": fonts.headingLetterSpacing,
                                                              "capitalization": Font.AllUppercase
                                                          }), Appearance.points(fonts.headingPixels))
    /// The running time in the header.
    readonly property font timer: fonts.followsSystem
                                  ? fonts.sized(Qt.font({
                                                            "family": Kirigami.Theme.fixedWidthFont.family,
                                                            "weight": Font.DemiBold
                                                        }), Kirigami.Theme.defaultFont.pointSize * 1.3)
                                  : fonts.designed(Appearance.monoFamily, fonts.timerPixels, Font.DemiBold)
    /// A tile's title.
    readonly property font tileTitle: fonts.followsSystem
                                      ? fonts.sized(Qt.font({
                                                                "family": Kirigami.Theme.defaultFont.family,
                                                                "weight": Font.DemiBold
                                                            }), Kirigami.Theme.smallFont.pointSize)
                                      : fonts.designed(Appearance.sansFamily, fonts.headingPixels, Font.DemiBold)
    /// The number on a tile, which a drag changes under the pointer.
    readonly property font readout: fonts.followsSystem
                                    ? fonts.sized(Qt.font({
                                                              "family": Kirigami.Theme.fixedWidthFont.family,
                                                              "weight": Font.DemiBold
                                                          }), Math.round(Kirigami.Theme.defaultFont.pointSize * 1.1))
                                    : fonts.designed(Appearance.monoFamily, fonts.readoutPixels, Font.DemiBold)
    /// The word under a tile's number, saying what it is.
    readonly property font tileCaption: fonts.followsSystem
                                        ? fonts.sized(Qt.font({
                                                                  "family": Kirigami.Theme.defaultFont.family
                                                              }), Kirigami.Theme.smallFont.pointSize * 0.9)
                                        : fonts.designed(Appearance.sansFamily, fonts.microPixels, Font.Normal)
    /// A text face's line: what the block is set to, in words, over a
    /// caption — the prototype's twelve pixels, semi-bold.
    readonly property font tileText: fonts.followsSystem
                                     ? fonts.sized(Qt.font({
                                                               "family": Kirigami.Theme.defaultFont.family,
                                                               "weight": Font.DemiBold
                                                           }), Kirigami.Theme.defaultFont.pointSize)
                                     : fonts.designed(Appearance.sansFamily, fonts.compactPixels, Font.DemiBold)
    /// A tile's one big reading, where it has no face of its own.
    readonly property font tileValue: fonts.followsSystem
                                      ? fonts.sized(Qt.font({
                                                                "family": Kirigami.Theme.defaultFont.family
                                                            }), Math.round(Kirigami.Theme.defaultFont.pointSize * 1.4))
                                      : fonts.designed(Appearance.monoFamily, fonts.readoutPixels, Font.DemiBold)
    /// The line under an icon face's drawing: which application,
    /// which device.
    readonly property font faceLine: fonts.followsSystem
                                     ? Kirigami.Theme.smallFont
                                     : fonts.designed(Appearance.sansFamily, fonts.tinyPixels, Font.Normal)
    /// What the session is doing, in the header: the prototype's
    /// semi-bold; the desktop's font while following the system.
    readonly property font headerStatus: fonts.followsSystem ? Kirigami.Theme.defaultFont : fonts.strong
    /// The status bar's words: the prototype's caption size.
    readonly property font statusBar: fonts.followsSystem
                                      ? Kirigami.Theme.defaultFont
                                      : fonts.designed(Appearance.sansFamily, fonts.captionPixels, Font.Normal)
    /// The library's lines: a category's heading, a block's name and
    /// what it does. While following the system, the desktop's font, bold
    /// for a heading, and its small font.
    readonly property font libraryHeading: fonts.followsSystem
                                           ? fonts.sized(Qt.font({
                                                                     "family": Kirigami.Theme.defaultFont.family,
                                                                     "weight": Font.Bold
                                                                 }), Kirigami.Theme.defaultFont.pointSize)
                                           : fonts.heading
    readonly property font libraryTitle: fonts.followsSystem ? Kirigami.Theme.defaultFont : fonts.strong
    readonly property font libraryDetail: fonts.followsSystem
                                          ? Kirigami.Theme.smallFont
                                          : fonts.designed(Appearance.sansFamily, fonts.detailPixels, Font.Normal)
    /// The numbers down a meter's side.
    readonly property font meterScale: fonts.followsSystem
                                       ? fonts.sized(Qt.font({
                                                                 "family": Kirigami.Theme.fixedWidthFont.family
                                                             }), Kirigami.Theme.smallFont.pointSize * 0.85)
                                       : fonts.designed(Appearance.monoFamily, fonts.microPixels, Font.Medium)
}

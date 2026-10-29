// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The type the interface is set in: the prototype's two faces and its
// scale in the Reverblabr appearance, the desktop's fonts when following the
// system.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "AppFonts"

    /// A font's size in CSS pixels. The conversion itself is held to the
    /// screen's DPI by test_appearance.cpp.
    function pixelsOf(font) {
        return font.pointSize / Appearance.points(1);
    }

    // Each test says which appearance it is about, and the suite goes
    // back to the application's default after it.
    function init() {
        Appearance.mode = Appearance.ReverblabrDark;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    // **The prototype's scale**, copied by hand from the sizes its
    // stylesheets set (the main window's, the application's and the block
    // tile's), largest first.
    function test_theScaleIsThePrototypes() {
        compare([AppFonts.timerPixels, AppFonts.readoutPixels, AppFonts.largePixels, AppFonts.bodyPixels,
                 AppFonts.compactPixels, AppFonts.captionPixels, AppFonts.headingPixels, AppFonts.detailPixels,
                 AppFonts.tinyPixels, AppFonts.microPixels],
                [17, 14, 13, 12.5, 12, 11.5, 11, 10.5, 10, 9.5]);
        compare([AppFonts.medium, AppFonts.demiBold, AppFonts.bold], [Font.Medium, Font.DemiBold, Font.Bold]);
    }

    // Each named use, in the face, weight and size the prototype sets it in.
    function test_eachUseIsSetAsThePrototypeSetsIt_data() {
        return [
            { tag: "body", family: "Noto Sans", pixels: 12.5, weight: Font.Normal },
            { tag: "strong", family: "Noto Sans", pixels: 12.5, weight: Font.DemiBold },
            { tag: "button", family: "Noto Sans", pixels: 12, weight: Font.DemiBold },
            { tag: "small", family: "Noto Sans", pixels: 11.5, weight: Font.Normal },
            { tag: "mono", family: "JetBrains Mono", pixels: 11, weight: Font.DemiBold },
            { tag: "heading", family: "Noto Sans", pixels: 11, weight: Font.Bold },
            { tag: "timer", family: "JetBrains Mono", pixels: 17, weight: Font.DemiBold },
            { tag: "tileTitle", family: "Noto Sans", pixels: 11, weight: Font.DemiBold },
            { tag: "readout", family: "JetBrains Mono", pixels: 14, weight: Font.DemiBold },
            { tag: "tileCaption", family: "Noto Sans", pixels: 9.5, weight: Font.Normal },
            { tag: "tileValue", family: "JetBrains Mono", pixels: 14, weight: Font.DemiBold },
            { tag: "meterScale", family: "JetBrains Mono", pixels: 9.5, weight: Font.Medium }
        ];
    }

    function test_eachUseIsSetAsThePrototypeSetsIt(data) {
        const font = AppFonts[data.tag];
        compare(font.family, data.family);
        compare(font.weight, data.weight);
        fuzzyCompare(testCase.pixelsOf(font), data.pixels, 0.01);
    }

    // A section heading is in capitals, spaced by 0.06 em, as the
    // prototype's `text-transform` and `letter-spacing` have it.
    function test_aHeadingIsSpacedCapitals() {
        compare(AppFonts.heading.capitalization, Font.AllUppercase);
        // To the 64th of a pixel a font keeps its spacing in.
        fuzzyCompare(AppFonts.heading.letterSpacing, 11 * 0.06, 1 / 64);
    }

    // **Bundled, so the faces are these on every machine.** The font
    // database has them from the application's own resources whether or
    // not the machine does, and a label set in them is drawn in them.
    function test_theFacesAreTheBundledOnes() {
        const families = Qt.fontFamilies();
        verify(families.indexOf("Noto Sans") >= 0, "Noto Sans is in the font database");
        verify(families.indexOf("JetBrains Mono") >= 0, "JetBrains Mono is in the font database");
        const label = createTemporaryQmlObject(
            "import QtQuick; import io.github.reverblabr; Text { font: AppFonts.timer; text: '0:00:00' }", testCase);
        compare(label.fontInfo.family, "JetBrains Mono");
        compare(label.fontInfo.weight, Font.DemiBold);
    }

    // **Following the system uses neither face**: every use is the desktop's
    // font, small font or fixed-width font, scaled as it was before the app
    // had its own look.
    function test_followingTheSystemIsTheDesktopsFonts() {
        Appearance.mode = Appearance.FollowSystem;
        const theme = testCase.Kirigami.Theme;
        compare(AppFonts.body, theme.defaultFont);
        compare(AppFonts.small, theme.smallFont);
        compare(AppFonts.mono, theme.fixedWidthFont);
        compare(AppFonts.sansFamily, theme.defaultFont.family);
        compare(AppFonts.monoFamily, theme.fixedWidthFont.family);
        compare(AppFonts.timer.family, theme.fixedWidthFont.family);
        fuzzyCompare(AppFonts.timer.pointSize, theme.defaultFont.pointSize * 1.3, 0.001);
        compare(AppFonts.readout.pointSize, Math.round(theme.defaultFont.pointSize * 1.1));
        fuzzyCompare(AppFonts.tileTitle.pointSize, theme.smallFont.pointSize, 0.001);
        compare(AppFonts.tileTitle.weight, Font.DemiBold);
        fuzzyCompare(AppFonts.tileCaption.pointSize, theme.smallFont.pointSize * 0.9, 0.001);
        compare(AppFonts.tileValue.pointSize, Math.round(theme.defaultFont.pointSize * 1.4));
        fuzzyCompare(AppFonts.meterScale.pointSize, theme.smallFont.pointSize * 0.85, 0.001);
    }

    // **The application's own font follows the choice**, since that is what
    // every control not named in AppFonts is set in: the body font in the
    // Reverblabr appearance, whatever the desktop gave when following it.
    function test_theApplicationFontFollowsTheChoice() {
        const text = "import QtQuick; Text { text: 'Run' }";
        Appearance.mode = Appearance.FollowSystem;
        const desktops = createTemporaryQmlObject(text, testCase);
        verify(Math.abs(testCase.pixelsOf(desktops.font) - 12.5) > 0.01 || desktops.font.family !== "Noto Sans",
               "following the system still sets text in the design's body font");
        Appearance.mode = Appearance.ReverblabrLight;
        const designs = createTemporaryQmlObject(text, testCase);
        compare(designs.font.family, "Noto Sans");
        fuzzyCompare(testCase.pixelsOf(designs.font), 12.5, 0.01);
    }
}

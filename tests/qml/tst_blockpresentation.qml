// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The catalogue's words and pictures as the interface meets them:
// that every block has a tint for its category, that every icon is a file
// Kirigami can really draw, and that the tints can be read on both the
// light and the dark canvas. The contrast ratios are measured here rather
// than judged by eye, and the numbers are written down beside them.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "BlockPresentation"
    when: windowShown

    // The canvas a block icon sits on in each theme: Breeze's alternate
    // window background, which is what AppColors.canvas resolves to. The
    // worse of the two backgrounds a scheme offers, so a tint that passes
    // here passes on the other one too.
    readonly property color lightCanvas: "#e3e5e7"
    readonly property color darkCanvas: "#292c30"

    Component {
        id: iconComponent

        Kirigami.Icon {
            // As the library and the canvas use them: one colour, given by
            // the interface, so the theme and the category tint decide it.
            isMask: true
            width: 24
            height: 24
        }
    }

    /// One channel of a colour, undone back to light (WCAG 2.1).
    function linear(channel) {
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
    }

    /// How much light a colour gives back, 0 for black and 1 for white.
    function luminance(colour) {
        return 0.2126 * linear(colour.r) + 0.7152 * linear(colour.g) + 0.0722 * linear(colour.b);
    }

    /// The contrast ratio between two colours, from 1 to 21.
    function contrast(first, second) {
        const a = luminance(first);
        const b = luminance(second);
        return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
    }

    // Every block in the catalogue, with the words and the picture it is
    // shown with. A type id with nothing behind it is a blank row.
    function test_everyBlockHasWordsAndAPicture() {
        const types = BlockPresentation.types;
        verify(types.length > 0, "the table has blocks in it");
        for (const type of types) {
            const block = BlockPresentation.describe(type);
            verify(block.name !== "", type + " has a name");
            verify(block.description !== "", type + " has a description");
            verify(block.tooltip !== "", type + " has a tooltip");
            verify(String(block.icon).endsWith(type + ".svg"), type + " has its own icon");
            verify(AppColors.tintsOnLightCanvas[block.category] !== undefined,
                   type + " is in a category with a tint: " + block.category);
            verify(AppColors.tintsOnDarkCanvas[block.category] !== undefined,
                   type + " is in a category with a tint: " + block.category);
        }
    }

    // A drawing that does not load is a hole in the library. Kirigami has to
    // be able to read every one of them, at the size they are drawn for.
    function test_everyIconRenders() {
        for (const type of BlockPresentation.types) {
            const icon = createTemporaryObject(iconComponent, testCase, {
                source: BlockPresentation.icon(type),
                color: AppColors.categoryTint(BlockPresentation.category(type))
            });
            verify(icon !== null, "an icon item for " + type);
            tryVerify(() => icon.status === Kirigami.Icon.Ready, 2000, type + " loads");
            verify(icon.valid, type + " is a picture");
            verify(icon.paintedWidth > 0 && icon.paintedHeight > 0, type + " is drawn");
        }
    }

    // A tint has to be readable on the canvas it is drawn on. 3:1 is the
    // WCAG 2.1 floor for something that is not text, which is what an icon
    // is; the measured ratios are here so that a change to a tint has to
    // change a number a person can see.
    function test_categoryTintContrast_data() {
        return [
            { tag: "sources on light", category: "sources", dark: false, ratio: 3.36 },
            { tag: "outputs on light", category: "outputs", dark: false, ratio: 3.34 },
            { tag: "effects on light", category: "effects", dark: false, ratio: 3.70 },
            { tag: "meters on light", category: "meters", dark: false, ratio: 3.39 },
            { tag: "advanced on light", category: "advanced", dark: false, ratio: 3.37 },
            { tag: "plugins on light", category: "plugins", dark: false, ratio: 3.38 },
            { tag: "hearing on light", category: "hearing", dark: false, ratio: 4.32 },
            { tag: "sources on dark", category: "sources", dark: true, ratio: 5.63 },
            { tag: "outputs on dark", category: "outputs", dark: true, ratio: 4.88 },
            { tag: "effects on dark", category: "effects", dark: true, ratio: 3.38 },
            { tag: "meters on dark", category: "meters", dark: true, ratio: 4.28 },
            { tag: "advanced on dark", category: "advanced", dark: true, ratio: 3.97 },
            { tag: "plugins on dark", category: "plugins", dark: true, ratio: 4.03 },
            { tag: "hearing on dark", category: "hearing", dark: true, ratio: 4.78 }
        ];
    }

    function test_categoryTintContrast(data) {
        const canvas = data.dark ? testCase.darkCanvas : testCase.lightCanvas;
        const tints = data.dark ? AppColors.tintsOnDarkCanvas : AppColors.tintsOnLightCanvas;
        const measured = contrast(Qt.color(tints[data.category]), canvas);
        verify(measured >= 3.0, data.tag + " is " + measured.toFixed(2) + ":1, which is under 3:1");
        fuzzyCompare(measured, data.ratio, 0.01, data.tag + " has moved to " + measured.toFixed(2) + ":1");
    }

    // The tint a block is actually drawn with is the one for its category on
    // the canvas of the moment, and never nothing at all.
    function test_categoryTintFollowsTheCanvas() {
        for (const category of Object.keys(AppColors.tints)) {
            compare(AppColors.categoryTint(category), Qt.color(AppColors.tints[category]), category);
        }
        // A category from a version we do not have is still drawn in
        // something, rather than in nothing.
        compare(AppColors.categoryTint("somethingLater"), AppColors.muted);
    }

    // **Which table that is, in each appearance**: the prototype's
    // own six on its dark canvas; on a light canvas, the prototype's or a
    // scheme's, the ones that clear 3:1.
    function test_eachAppearanceDrawsWithItsOwnTints_data() {
        return [
            { tag: "Reverblabr dark", mode: Appearance.ReverblabrDark, table: "designTints" },
            { tag: "Reverblabr light", mode: Appearance.ReverblabrLight, table: "tintsOnLightCanvas" }
        ];
    }

    function test_eachAppearanceDrawsWithItsOwnTints(data) {
        Appearance.mode = data.mode;
        const table = AppColors[data.table];
        compare(AppColors.tints, table);
        for (const category of Object.keys(table)) {
            compare(AppColors.categoryTint(category), Qt.color(table[category]), data.tag + ": " + category);
        }
    }

    // The prototype's own hues, on the prototype's own dark canvas.
    function test_theDarkAppearancesTintsClearThreeToOne() {
        for (const category of Object.keys(AppColors.designTints)) {
            const measured = contrast(Qt.color(AppColors.designTints[category]), Qt.color(AppColors.darkPalette.canvas));
            verify(measured >= 3.0, category + " is " + measured.toFixed(2) + ":1 on the dark canvas");
        }
    }

    // Each test says which appearance it is about, and the suite goes
    // back to the application's default after it.
    function init() {
        Appearance.mode = Appearance.ReverblabrDark;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }
}

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The sizes the interface is built to: the prototype's in the
// Reverblabr appearance, Kirigami's units when following the system.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "AppMetrics"

    // Each test says which appearance it is about, and the suite goes
    // back to the application's default after it.
    function init() {
        Appearance.mode = Appearance.ReverblabrDark;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    // **Every measure, as the prototype has it**, copied by hand from its
    // stylesheets (the main window's and the block tile's): the
    // `border-radius`, `gap`, `padding`, `height` and `box-shadow` values it
    // writes, under the names the interface asks for them by.
    function test_everyMeasureIsThePrototypes_data() {
        return [
            { tag: "barRadius", pixels: 1.5 },
            { tag: "troughRadius", pixels: 2 },
            { tag: "smallRadius", pixels: 3 },
            { tag: "controlRadius", pixels: 4 },
            { tag: "tileRadius", pixels: 5 },
            { tag: "panelRadius", pixels: 6 },
            { tag: "windowRadius", pixels: 8 },
            { tag: "hairSpacing", pixels: 2 },
            { tag: "smallSpacing", pixels: 4 },
            { tag: "mediumSpacing", pixels: 6 },
            { tag: "largeSpacing", pixels: 8 },
            { tag: "rowSpacing", pixels: 10 },
            { tag: "panelPadding", pixels: 12 },
            { tag: "groupSpacing", pixels: 14 },
            { tag: "wideSpacing", pixels: 16 },
            { tag: "shadowSize", pixels: 24 },
            { tag: "shadowOffset", pixels: 8 },
            { tag: "liftedShadowSize", pixels: 40 },
            { tag: "liftedShadowOffset", pixels: 16 },
            { tag: "headerHeight", pixels: 40 },
            { tag: "statusBarHeight", pixels: 30 },
            { tag: "smallControlHeight", pixels: 24 },
            { tag: "mediumControlHeight", pixels: 26 },
            { tag: "controlHeight", pixels: 28 },
            { tag: "sidebarTabHeight", pixels: 30 },
            { tag: "panelHeaderHeight", pixels: 36 },
            { tag: "sidebarWidth", pixels: 252 },
            { tag: "tileWidth", pixels: 132 },
            { tag: "tileHeight", pixels: 96 },
            { tag: "tileHeaderHeight", pixels: 24 },
            // The controls: `.btn`, the Run button, the check boxes,
            // radio buttons, switches and segmented rows of a panel, and a
            // library row.
            { tag: "buttonPadding", pixels: 12 },
            { tag: "primaryButtonPadding", pixels: 18 },
            { tag: "buttonSpacing", pixels: 8 },
            { tag: "buttonIconSize", pixels: 16 },
            { tag: "indicatorSize", pixels: 15 },
            { tag: "radioDotSize", pixels: 7 },
            { tag: "switchWidth", pixels: 34 },
            { tag: "switchHeight", pixels: 18 },
            { tag: "switchKnobSize", pixels: 14 },
            { tag: "switchKnobInset", pixels: 2 },
            { tag: "segmentHeight", pixels: 22 },
            { tag: "segmentInset", pixels: 2 },
            { tag: "delegateHorizontalPadding", pixels: 6 },
            { tag: "delegateVerticalPadding", pixels: 5 },
            { tag: "focusRingWidth", pixels: 2 },
            { tag: "disabledOpacity", pixels: 0.45 },
            // Where things are: the prototype's tile, the
            // panel's rows and header, the canvas's overlays, the sidebar.
            { tag: "tileOuterWidth", pixels: 132 },
            { tag: "tileOuterHeight", pixels: 96 },
            { tag: "tileBandHeight", pixels: 24 },
            { tag: "tileHeaderInset", pixels: 9 },
            { tag: "tileHeaderSpacing", pixels: 6 },
            { tag: "tileIconSize", pixels: 13 },
            { tag: "stateDotSize", pixels: 7 },
            { tag: "faceIconSize", pixels: 24 },
            { tag: "faceSpacing", pixels: 5 },
            { tag: "faceMargin", pixels: 8 },
            { tag: "connectorSize", pixels: 9 },
            { tag: "canvasGridSpacing", pixels: 20 },
            { tag: "overlayInset", pixels: 16 },
            { tag: "zoomLabelWidth", pixels: 34 },
            { tag: "settingsWidth", pixels: 322 },
            { tag: "settingsGap", pixels: 16 },
            { tag: "settingsMargin", pixels: 8 },
            { tag: "caretSize", pixels: 10 },
            { tag: "caretTop", pixels: 23 },
            { tag: "panelSidePadding", pixels: 12 },
            { tag: "panelEndPadding", pixels: 5 },
            { tag: "panelRowGap", pixels: 10 },
            { tag: "panelHeaderLeftPadding", pixels: 12 },
            { tag: "panelHeaderRightPadding", pixels: 6 },
            { tag: "panelHeaderSpacing", pixels: 8 },
            { tag: "panelIconSize", pixels: 14 },
            { tag: "panelFooterPadding", pixels: 8 },
            { tag: "panelLabelWidth", pixels: 96 },
            { tag: "panelNarrowLabelWidth", pixels: 60 },
            { tag: "labelSpacing", pixels: 10 },
            { tag: "pullIntoRow", pixels: -6 },
            { tag: "sectionTopPadding", pixels: 9 },
            { tag: "sectionBottomPadding", pixels: 5 },
            { tag: "sectionChevronSize", pixels: 10 },
            { tag: "sidebarTabsTop", pixels: 8 },
            { tag: "sidebarTabsBottom", pixels: 4 },
            { tag: "searchMargin", pixels: 10 },
            { tag: "libraryInset", pixels: 6 },
            { tag: "libraryIconSize", pixels: 15 },
            { tag: "libraryTitleLine", pixels: 15 },
            { tag: "libraryDetailLine", pixels: 13 }
        ];
    }

    function test_everyMeasureIsThePrototypes(data) {
        for (const mode of [Appearance.ReverblabrDark, Appearance.ReverblabrLight]) {
            Appearance.mode = mode;
            compare(AppMetrics[data.tag], data.pixels);
        }
    }

    // **Following the system is Kirigami's units**, which is what every
    // corner, gap and shadow used before the app had its own look.
    function test_followingTheSystemIsKirigamisUnits() {
        Appearance.mode = Appearance.FollowSystem;
        compare(AppMetrics.controlRadius, Kirigami.Units.cornerRadius);
        compare(AppMetrics.tileRadius, Kirigami.Units.cornerRadius);
        compare(AppMetrics.panelRadius, Kirigami.Units.cornerRadius);
        compare(AppMetrics.barRadius, 1);
        compare(AppMetrics.troughRadius, 2);
        compare(AppMetrics.smallSpacing, Kirigami.Units.smallSpacing);
        compare(AppMetrics.mediumSpacing, Kirigami.Units.mediumSpacing);
        compare(AppMetrics.largeSpacing, Kirigami.Units.largeSpacing);
        compare(AppMetrics.shadowSize, Kirigami.Units.gridUnit);
        compare(AppMetrics.shadowOffset, Kirigami.Units.smallSpacing);
        compare(AppMetrics.liftedShadowSize, Kirigami.Units.gridUnit * 2);
        compare(AppMetrics.liftedShadowOffset, Kirigami.Units.smallSpacing * 2);
    }

    // **Where things are, while following the system, is where they were**:
    // each measure is the expression the place that uses it wrote
    // before it had a name.
    function test_followingTheSystemKeepsItsOwnSpacing() {
        Appearance.mode = Appearance.FollowSystem;
        compare(AppMetrics.tileOuterWidth, Math.round(Kirigami.Units.gridUnit * 7.5));
        compare(AppMetrics.tileOuterHeight, Math.round(Kirigami.Units.gridUnit * 5.5));
        compare(AppMetrics.tileBandHeight, Math.round(Kirigami.Units.gridUnit * 4 / 3));
        compare(AppMetrics.connectorSize, Math.round(Kirigami.Units.gridUnit / 2));
        compare(AppMetrics.canvasGridSpacing, 24);
        compare(AppMetrics.settingsWidth, Kirigami.Units.gridUnit * 18);
        compare(AppMetrics.settingsGap, Kirigami.Units.gridUnit);
        compare(AppMetrics.panelSidePadding, Kirigami.Units.largeSpacing);
        compare(AppMetrics.panelRowGap, Kirigami.Units.smallSpacing);
        compare(AppMetrics.panelLabelWidth, Kirigami.Units.gridUnit * 6);
        compare(AppMetrics.labelSpacing, Kirigami.Units.largeSpacing);
        compare(AppMetrics.pullIntoRow, 0);
        compare(AppMetrics.sectionTopPadding, Kirigami.Units.smallSpacing);
        compare(AppMetrics.libraryInset, Kirigami.Units.largeSpacing);
    }
}

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

// The Reverblabr control style ([ADR-0012]): each control drawn as
// the prototype draws it, from the tokens, and Kirigami and Qt's own
// controls given the design's colours.
//
// **Most of this only means something under that style**, which the suite's
// second run (`qml.mainwindow.reverblabr-style`) uses; under the first,
// those tests say so and skip. The segmented frame is the application's own
// and is tested under both.
TestCase {
    id: testCase

    name: "ControlStyle"
    when: windowShown

    // **In a window of its own**: a control that is not shown is not
    // hovered, and has no keyboard focus to draw a ring for.
    QQC2.ApplicationWindow {
        id: window

        width: 640
        height: 480
        visible: true

        ColumnLayout {
            x: 20
            y: 20

            RowLayout {
                QQC2.Button {
                    id: plain

                    text: "Plain"
                }

                QQC2.Button {
                    id: primary

                    text: "Primary"
                    highlighted: true
                }

                QQC2.Button {
                    id: flat

                    text: "Flat"
                    flat: true
                }

                QQC2.Button {
                    id: disabledButton

                    text: "Disabled"
                    enabled: false
                }

                QQC2.ToolButton {
                    id: tool

                    icon.name: "list-add"
                    display: QQC2.AbstractButton.IconOnly
                }
            }

            RowLayout {
                QQC2.CheckBox {
                    id: check

                    text: "Check"
                }

                QQC2.RadioButton {
                    id: radio

                    text: "Radio"
                }

                QQC2.Switch {
                    id: toggle

                    text: "Switch"
                }
            }

            QQC2.TabBar {
                id: tabs

                Layout.preferredWidth: 240

                QQC2.TabButton {
                    id: firstTab

                    text: "Blocks"
                }

                QQC2.TabButton {
                    id: secondTab

                    text: "Info"
                }
            }

            QQC2.ItemDelegate {
                id: row

                text: "A row"
            }

            SegmentedFrame {
                id: segments

                Layout.preferredWidth: 240

                Repeater {
                    id: segmentRepeater

                    model: ["Off", "2×", "3×"]

                    delegate: QQC2.Button {
                        required property string modelData

                        text: modelData
                        checkable: true
                        autoExclusive: true
                        flat: segments.segmented
                        Layout.preferredHeight: segments.segmentHeight
                        Layout.fillWidth: true
                    }
                }
            }

            Item {
                id: kirigamiItem

                implicitWidth: 10
                implicitHeight: 10
            }

            Item {
                id: selectionItem

                implicitWidth: 10
                implicitHeight: 10
                Kirigami.Theme.inherit: false
                Kirigami.Theme.colorSet: Kirigami.Theme.Selection
            }
        }
    }

    Component {
        id: loneButton

        QQC2.Button {
            text: "Focus"
        }
    }

    function init() {
        Appearance.mode = Appearance.ReverblabrDark;
        check.checked = false;
        radio.checked = false;
        toggle.checked = false;
        tabs.currentIndex = 0;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    function requireOwnStyle() {
        if (!Appearance.ownStyle) {
            skip("only under the Reverblabr control style");
        }
    }

    // The ring round a control: the one child of its background that is a
    // hollow shape outside it.
    function focusRingOf(control) {
        for (const child of control.background.children) {
            if (child.border !== undefined && child.color.a === 0 && child.anchors.margins < 0) {
                return child;
            }
        }
        return null;
    }

    function test_theStyleIsTheOneTheApplicationChose() {
        requireOwnStyle();
        verify(Appearance.ownStyle);
    }

    // `.btn` in the prototype: a raised surface in a border, 26 high, a
    // 4-pixel corner, 12-pixel semibold words in the text colour.
    function test_aButtonIsTheDesignsButton() {
        requireOwnStyle();
        compare(plain.background.color, AppColors.surface2);
        compare(plain.background.border.color, AppColors.controlBorder);
        compare(plain.background.radius, AppMetrics.controlRadius);
        compare(plain.implicitHeight, AppMetrics.mediumControlHeight);
        compare(plain.leftPadding, AppMetrics.buttonPadding);
        compare(plain.font.pointSize, AppFonts.button.pointSize);
        compare(plain.font.weight, Font.DemiBold);
        compare(plain.contentItem.color, AppColors.text);
    }

    function test_aPrimaryButtonIsFilledWithTheAccent() {
        requireOwnStyle();
        compare(primary.background.color, AppColors.accent);
        compare(primary.contentItem.color, AppColors.textOnAccent);
    }

    // A flat button is a segment of a segmented row: nothing until it is
    // chosen, and then the accent; a smaller corner.
    function test_aFlatButtonIsASegment() {
        requireOwnStyle();
        compare(flat.background.color.a, 0);
        compare(flat.background.radius, AppMetrics.smallRadius);
        flat.checkable = true;
        flat.checked = true;
        compare(flat.background.color, AppColors.accent);
        flat.checked = false;
        flat.checkable = false;
    }

    function test_aDisabledControlIsFaded() {
        requireOwnStyle();
        compare(disabledButton.opacity, AppMetrics.disabledOpacity);
        compare(plain.opacity, 1);
    }

    function test_aToolButtonIsAMutedSquare() {
        requireOwnStyle();
        compare(tool.implicitWidth, AppMetrics.controlHeight);
        compare(tool.implicitHeight, AppMetrics.controlHeight);
        compare(tool.contentItem.color, AppColors.muted);
        compare(tool.background.color.a, 0);
    }

    function test_aCheckBoxIsTheDesignsBox() {
        requireOwnStyle();
        compare(check.indicator.width, AppMetrics.indicatorSize);
        compare(check.indicator.radius, AppMetrics.smallRadius);
        compare(check.indicator.color, AppColors.field);
        compare(check.spacing, AppMetrics.rowSpacing);
        check.checked = true;
        compare(check.indicator.color, AppColors.accent);
    }

    function test_aRadioButtonIsARingWithADot() {
        requireOwnStyle();
        compare(radio.indicator.width, AppMetrics.indicatorSize);
        compare(radio.indicator.border.color, AppColors.controlBorder);
        const dot = radio.indicator.children[0];
        compare(dot.width, AppMetrics.radioDotSize);
        verify(!dot.visible);
        radio.checked = true;
        compare(radio.indicator.border.color, AppColors.accent);
        verify(dot.visible);
    }

    // A 34 × 18 pill with a 14-pixel knob two pixels in from either end.
    function test_aSwitchIsThePill() {
        requireOwnStyle();
        compare(toggle.indicator.width, AppMetrics.switchWidth);
        compare(toggle.indicator.height, AppMetrics.switchHeight);
        compare(toggle.indicator.color, AppColors.well);
        const knob = toggle.indicator.children[0];
        compare(knob.width, AppMetrics.switchKnobSize);
        tryCompare(knob, "x", AppMetrics.switchKnobInset);
        toggle.checked = true;
        compare(toggle.indicator.color, AppColors.accent);
        tryCompare(knob, "x", AppMetrics.switchWidth - AppMetrics.switchKnobSize - AppMetrics.switchKnobInset);
    }

    // The library's tabs: the one in front raised and heavier, the other
    // muted words on nothing.
    function test_aTabIsTheLibrarysTab() {
        requireOwnStyle();
        compare(firstTab.implicitHeight, AppMetrics.sidebarTabHeight);
        compare(firstTab.background.color, AppColors.surface2);
        compare(firstTab.background.border.width, 1);
        compare(firstTab.font.weight, Font.DemiBold);
        compare(secondTab.background.color.a, 0);
        compare(secondTab.contentItem.color, AppColors.muted);
        compare(tabs.spacing, AppMetrics.hairSpacing);
    }

    function test_aRowIsTheLibrarysRow() {
        requireOwnStyle();
        compare(row.leftPadding, AppMetrics.delegateHorizontalPadding);
        compare(row.topPadding, AppMetrics.delegateVerticalPadding);
        compare(row.background.radius, AppMetrics.controlRadius);
        compare(row.background.color.a, 0);
    }

    // The keyboard's place is marked with the design's own ring, the soft
    // accent band it draws round a chosen tile.
    function test_theKeyboardsPlaceIsRinged() {
        requireOwnStyle();
        // **A button in the test's own window**: the keyboard focus of a
        // second window is the platform's to give, and offscreen it never
        // does.
        const button = createTemporaryObject(loneButton, testCase);
        const ring = testCase.focusRingOf(button);
        verify(ring, "the button has a focus ring");
        verify(!ring.shown);
        button.forceActiveFocus(Qt.TabFocusReason);
        verify(button.visualFocus, "the button has the keyboard");
        verify(ring.shown);
        compare(ring.border.color, AppColors.accentSoft);
        compare(ring.border.width, AppMetrics.focusRingWidth);
        compare(button.background.border.color, AppColors.accent);
    }

    // **Kirigami draws in the design's colours** through the platform
    // plugin, not in its own built-in light theme.
    function test_kirigamiHasTheDesignsColours() {
        requireOwnStyle();
        for (const mode of [Appearance.ReverblabrDark, Appearance.ReverblabrLight]) {
            Appearance.mode = mode;
            tryCompare(kirigamiItem.Kirigami.Theme, "backgroundColor", AppColors.window);
            compare(kirigamiItem.Kirigami.Theme.textColor, AppColors.text);
            compare(kirigamiItem.Kirigami.Theme.highlightColor, AppColors.accent);
            compare(kirigamiItem.Kirigami.Theme.negativeTextColor, AppColors.danger);
            compare(selectionItem.Kirigami.Theme.backgroundColor, AppColors.accent);
            compare(selectionItem.Kirigami.Theme.textColor, AppColors.textOnAccent);
        }
    }

    // **And so do Qt's own controls** where the style has none yet: they
    // draw with the application palette.
    function test_theApplicationPaletteIsTheDesigns() {
        requireOwnStyle();
        for (const mode of [Appearance.ReverblabrDark, Appearance.ReverblabrLight]) {
            Appearance.mode = mode;
            tryCompare(plain.palette, "window", AppColors.window);
            compare(plain.palette.base, AppColors.field);
            compare(plain.palette.windowText, AppColors.text);
            compare(plain.palette.highlight, AppColors.accent);
        }
    }

    // The application's own segmented row, in both styles: framed segments
    // in the Reverblabr appearance, the desktop's buttons side by side when
    // following the system.
    function test_aSegmentedRowIsFramedOnlyInTheDesignsLook() {
        verify(segments.segmented);
        verify(segments.frameRectangle.visible);
        compare(segments.frameRectangle.color, AppColors.field);
        compare(segments.row.spacing, AppMetrics.segmentInset);
        tryCompare(segmentRepeater.itemAt(0), "height", AppMetrics.segmentHeight);
        verify(segmentRepeater.itemAt(0).flat);

        Appearance.mode = Appearance.FollowSystem;
        verify(!segments.segmented);
        verify(!segments.frameRectangle.visible);
        compare(segments.inset, 0);
        compare(segments.row.spacing, 0);
        verify(!segmentRepeater.itemAt(0).flat);
    }
}

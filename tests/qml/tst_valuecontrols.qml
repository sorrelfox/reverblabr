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

// The value controls of the Reverblabr control style ([ADR-0012]):
// sliders, the lists that drop down, text fields, numbers and scroll bars,
// each drawn as the prototype draws it, from the tokens.
//
// **Most of this only means something under that style**, which the suite's
// second run (`qml.mainwindow.reverblabr-style`) uses; under the first,
// those tests say so and skip. A tile's slider, the application list and
// the panel's scrolling are the application's own and are tested under
// both.
TestCase {
    id: testCase

    name: "ValueControls"
    when: windowShown

    property string sessionId: ""

    // **In a window of its own**: a control that is not shown is not
    // hovered, and has no keyboard focus.
    QQC2.ApplicationWindow {
        id: window

        width: 640
        height: 560
        visible: true

        ColumnLayout {
            x: 20
            y: 20
            width: 300

            QQC2.Slider {
                id: slider

                Layout.fillWidth: true
                from: 0
                to: 100
                value: 50
            }

            FaceSlider {
                id: faceSlider

                Layout.fillWidth: true
                tint: AppColors.tints.effects
                from: 0
                to: 100
                value: 25
            }

            QQC2.ComboBox {
                id: combo

                model: ["Manual", "Podcast voice", "Music"]
            }

            QQC2.TextField {
                id: field

                Layout.fillWidth: true
                placeholderText: "Opened when the session starts"
            }

            QQC2.SpinBox {
                id: spin

                from: 0
                to: 10
                value: 5
            }

            QQC2.TextArea {
                id: area

                Layout.fillWidth: true
            }

            ListView {
                id: list

                Layout.fillWidth: true
                Layout.preferredHeight: 60
                clip: true
                model: 20
                delegate: QQC2.Label {
                    required property int index

                    text: "Row " + index
                    height: 20
                }
                QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                    id: bar
                }
            }

            ListView {
                id: shortList

                Layout.fillWidth: true
                Layout.preferredHeight: 60
                model: 2
                delegate: QQC2.Label {
                    required property int index

                    text: "Row " + index
                    height: 20
                }
                QQC2.ScrollBar.vertical: QQC2.ScrollBar {
                    id: shortBar
                }
            }
        }

        SettingsPanel {
            id: panel

            x: 330
            y: 20
            width: 290
        }
    }

    // **Controls in the test's own window**, for the keyboard: the
    // keyboard focus of a second window is the platform's to give, and
    // offscreen it never does.
    Component {
        id: loneSlider

        QQC2.Slider {
            width: 200
            from: 0
            to: 100
            value: 50
        }
    }

    Component {
        id: loneCombo

        QQC2.ComboBox {
            model: ["Manual", "Podcast voice", "Music"]
        }
    }

    Component {
        id: loneField

        QQC2.TextField {
            width: 200
        }
    }

    // The canvas's own panel, opened and closed as the canvas does it.
    Component {
        id: shownCanvas

        Window {
            property alias canvas: inner

            width: 900
            height: 700
            visible: true

            SessionCanvas {
                id: inner

                anchors.fill: parent
            }
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Value controls");
        verify(testCase.sessionId !== "", "the mock opened a session");
        // A tall panel and a short one, for the scrolling; an application
        // block for its list.
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "addBlock", block: { id: "meter", type: "meter.peakrms" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "addBlock", block: { id: "out", type: "output.device" }, index: -1 }));
        TestEngine.applyEdit(testCase.sessionId, JSON.stringify({
            op: "addBlock", block: { id: "app", type: "source.application" }, index: -1 }));
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function init() {
        Appearance.mode = Appearance.ReverblabrDark;
        slider.value = 50;
        field.text = "";
        spin.value = 5;
        combo.popup.close();
    }

    function cleanup() {
        panel.close();
        Appearance.mode = Appearance.FollowSystem;
    }

    function requireOwnStyle() {
        if (!Appearance.ownStyle) {
            skip("only under the Reverblabr control style");
        }
    }

    // **Every measure, as the prototype has it**: a panel's field is
    // `height:26px; padding:0 8px`, a list's `padding:0 6px`, a tile's
    // slider a four-pixel trough under a twelve-pixel ring, a panel's a
    // sixteen-pixel row, and a scroll bar the browser's thin one.
    function test_everyMeasureIsThePrototypes_data() {
        return [
            { tag: "sliderGrooveHeight", pixels: 4 },
            { tag: "faceSliderHandleSize", pixels: 12 },
            { tag: "sliderHandleSize", pixels: 16 },
            { tag: "sliderHandleBorder", pixels: 2 },
            { tag: "fieldPadding", pixels: 8 },
            { tag: "comboPadding", pixels: 6 },
            { tag: "fieldVerticalPadding", pixels: 4 },
            { tag: "fieldWidth", pixels: 140 },
            { tag: "chevronWidth", pixels: 8 },
            { tag: "scrollBarWidth", pixels: 8 },
            { tag: "scrollThumbWidth", pixels: 4 },
        ];
    }

    function test_everyMeasureIsThePrototypes(data) {
        compare(AppMetrics[data.tag], data.pixels);
    }

    // --- sliders ---

    Component {
        id: bipolarFaceSlider

        FaceSlider {
            width: 200
        }
    }

    // The prototype's own slider, the one on a Volume tile: a trough in the
    // well colour filled to the value, and a ring with the surface inside.
    function test_aSliderIsTheTilesSliderInTheAccent() {
        requireOwnStyle();
        compare(slider.background.height, AppMetrics.sliderGrooveHeight);
        compare(slider.background.color, AppColors.well);
        compare(slider.background.children[0].color, AppColors.accent);
        compare(slider.background.children[0].width, slider.background.width / 2);
        compare(slider.handle.width, AppMetrics.sliderHandleSize);
        compare(slider.handle.radius, AppMetrics.sliderHandleSize / 2);
        compare(slider.handle.color, AppColors.surface);
        compare(slider.handle.border.width, AppMetrics.sliderHandleBorder);
        compare(slider.handle.border.color, AppColors.accent);
        compare(slider.implicitHeight, AppMetrics.sliderHandleSize);
    }

    // **One drawing for both**: a tile's slider is the same groove and ring,
    // smaller and in its block's tint, in whichever style the window has.
    function test_aTilesSliderIsTheSameDrawingInItsTint() {
        compare(faceSlider.background.color, AppColors.well);
        compare(faceSlider.background.height, AppMetrics.sliderGrooveHeight);
        compare(faceSlider.background.children[0].color, AppColors.tints.effects);
        compare(faceSlider.handle.width, AppMetrics.faceSliderHandleSize);
        compare(faceSlider.handle.color, AppColors.surface);
        compare(faceSlider.handle.border.color, AppColors.tints.effects);
    }

    // A setting either side of zero — a Balance, a Pan, a boost or a
    // cut — fills from zero to the value, not from the left edge; a range
    // that does not cross zero fills from its start as it always has.
    function test_aSliderEitherSideOfZeroFillsFromZero_data() {
        return [
            { tag: "left of the middle", from: -100, to: 100, value: -30, start: 0.35, end: 0.5 },
            { tag: "right of the middle", from: -100, to: 100, value: 60, start: 0.5, end: 0.8 },
            { tag: "at the middle", from: -100, to: 100, value: 0, start: 0.5, end: 0.5 },
            { tag: "a cut on an uneven range", from: -12, to: 24, value: -6, start: 1 / 6, end: 1 / 3 },
            { tag: "a range from zero", from: 0, to: 100, value: 25, start: 0, end: 0.25 },
            { tag: "a range below zero", from: -80, to: 0, value: -20, start: 0, end: 0.75 },
        ];
    }

    function test_aSliderEitherSideOfZeroFillsFromZero(data) {
        const face = createTemporaryObject(bipolarFaceSlider, window.contentItem,
                                           { from: data.from, to: data.to, value: data.value });
        const groove = face.background;
        const fill = groove.children[0];
        fuzzyCompare(fill.x, data.start * groove.width, 0.01);
        fuzzyCompare(fill.width, (data.end - data.start) * groove.width, 0.01);
    }

    function test_aSliderMovesWithTheKeyboardAndIsRinged() {
        requireOwnStyle();
        const lone = createTemporaryObject(loneSlider, testCase);
        verify(!lone.handle.children[0].shown);
        lone.forceActiveFocus(Qt.TabFocusReason);
        verify(lone.visualFocus, "the slider did not take the keyboard");
        compare(lone.handle.border.color, AppColors.accent);
        verify(lone.handle.children[0].shown, "the keyboard's place was not ringed");
        keyClick(Qt.Key_Right);
        verify(lone.value > 50, "the arrow key did not move the slider");
    }

    // --- lists that drop down ---

    // `select` in a panel: the field's colour in a border, 26 high, a
    // 4-pixel corner, the choice six pixels in and a chevron at the end.
    function test_aListIsAFieldWithAChevron() {
        requireOwnStyle();
        compare(combo.implicitHeight, AppMetrics.mediumControlHeight);
        compare(combo.background.color, AppColors.field);
        compare(combo.background.border.color, AppColors.controlBorder);
        compare(combo.background.radius, AppMetrics.controlRadius);
        compare(combo.indicator.width, AppMetrics.chevronWidth);
        compare(combo.indicator.up, false);
        compare(combo.leftPadding, AppMetrics.comboPadding);
        compare(combo.contentItem.color, AppColors.text);
        compare(combo.font.pointSize, AppFonts.body.pointSize);
    }

    function test_anOpenListIsFramedInTheAccentBelowItsField() {
        requireOwnStyle();
        combo.popup.open();
        tryVerify(() => combo.popup.opened, 1000, "the list did not open");
        compare(combo.background.border.color, AppColors.accent);
        compare(combo.indicator.up, true);
        compare(combo.popup.background.color, AppColors.field);
        compare(combo.popup.background.border.color, AppColors.border);
        verify(combo.popup.y >= combo.height, "the list did not open below its field");
        compare(combo.popup.width, combo.width);
        combo.popup.close();
        tryVerify(() => !combo.popup.visible, 1000);
    }

    function test_aListIsChosenFromWithTheKeyboard() {
        requireOwnStyle();
        const lone = createTemporaryObject(loneCombo, testCase);
        lone.forceActiveFocus(Qt.TabFocusReason);
        verify(lone.visualFocus, "the list did not take the keyboard");
        compare(lone.background.border.color, AppColors.accent);
        keyClick(Qt.Key_Down);
        compare(lone.currentIndex, 1);
        compare(lone.displayText, "Podcast voice");
    }

    // --- text ---

    function test_aTextFieldIsThePanelsField() {
        requireOwnStyle();
        compare(field.implicitHeight, AppMetrics.mediumControlHeight);
        compare(field.leftPadding, AppMetrics.fieldPadding);
        compare(field.rightPadding, AppMetrics.fieldPadding);
        compare(field.background.color, AppColors.field);
        compare(field.background.border.color, AppColors.controlBorder);
        compare(field.background.radius, AppMetrics.controlRadius);
        compare(field.color, AppColors.text);
        compare(field.placeholderTextColor, AppColors.muted);
        compare(field.selectionColor, AppColors.accent);
        compare(field.selectedTextColor, AppColors.textOnAccent);
    }

    // `style-focus="border-color:var(--accent)"`: typed into, the border
    // takes the accent, and nothing else changes.
    function test_aFieldTypedIntoIsBorderedInTheAccent() {
        requireOwnStyle();
        const lone = createTemporaryObject(loneField, testCase);
        compare(lone.background.border.color, AppColors.controlBorder);
        lone.forceActiveFocus();
        compare(lone.background.border.color, AppColors.accent);
        keyClick(Qt.Key_O);
        keyClick(Qt.Key_B);
        keyClick(Qt.Key_S);
        compare(lone.text, "obs");
        lone.focus = false;
        compare(lone.background.border.color, AppColors.controlBorder);
    }

    function test_aTextAreaIsATallerField() {
        requireOwnStyle();
        compare(area.background.color, AppColors.field);
        compare(area.background.border.color, AppColors.controlBorder);
        compare(area.leftPadding, AppMetrics.fieldPadding);
        verify(area.implicitHeight > AppMetrics.mediumControlHeight, "a text area is no taller than a field");
    }

    // A number: the field, with a pair of chevrons stacked at its end.
    function test_aNumberIsAFieldWithStepsAtItsEnd() {
        requireOwnStyle();
        compare(spin.background.color, AppColors.field);
        compare(spin.up.indicator.width, AppMetrics.stepperWidth);
        compare(spin.up.indicator.x, spin.width - AppMetrics.stepperWidth);
        compare(spin.down.indicator.x, spin.width - AppMetrics.stepperWidth);
        verify(spin.down.indicator.y >= spin.up.indicator.y + spin.up.indicator.height - 1,
               "the steps are not stacked, up above down");
        mouseClick(spin.up.indicator);
        compare(spin.value, 6);
        mouseClick(spin.down.indicator);
        mouseClick(spin.down.indicator);
        compare(spin.value, 4);
    }

    // --- scroll bars ---

    // `scrollbar-width: thin; scrollbar-color: var(--well) transparent`,
    // shown whenever there is more to scroll to, as the prototype's are.
    function test_aScrollBarIsThinAndInTheWellColour() {
        requireOwnStyle();
        compare(bar.width, AppMetrics.scrollBarWidth);
        verify(bar.contentItem.visible, "a list with more below did not show its scroll bar");
        const thumb = bar.contentItem.children[0];
        compare(thumb.width, AppMetrics.scrollThumbWidth);
        compare(thumb.color, AppColors.well);
        verify(!shortBar.contentItem.visible, "a list with nothing more to show had a scroll bar");
    }

    // Held, the thumb widens and takes the muted colour, so what is being
    // dragged is plain; let go, it is the thin one again.
    function test_aScrollBarWidensWhileItIsHeld() {
        requireOwnStyle();
        const thumb = bar.contentItem.children[0];
        bar.pressed = true;
        compare(thumb.width, AppMetrics.scrollThumbActiveWidth);
        compare(thumb.color, AppColors.muted);
        bar.pressed = false;
        compare(thumb.width, AppMetrics.scrollThumbWidth);
        compare(thumb.color, AppColors.well);
    }

    // --- the application's own ---

    // **The panel scrolls as far as what is in it, and no further**.
    // The view used to keep the tallest panel it had shown, so after a
    // Peak/RMS panel an Output Device's scrolled on into nothing — which the
    // style's scroll bar, shown whenever there is more, said out loud.
    function test_aShortPanelAfterATallOneHasNothingToScroll() {
        const shown = createTemporaryObject(shownCanvas, null);
        const canvas = shown.canvas;
        const panel = canvas.settings;
        const tallBlock = canvas.addBlockAt("meter.peakrms", 120, 150);
        const shortBlock = canvas.addBlockAt("output.device", 120, 350);
        canvas.openSettings(tallBlock);
        tryVerify(() => panel.body.item !== null && panel.body.item.blockId === tallBlock, 1000);
        // Every section of it open, so it is as tall as it gets.
        const unfold = item => {
            for (const child of item.children) {
                if (child.toggle !== undefined && child.expanded !== undefined) {
                    child.expanded = true;
                }
                unfold(child);
            }
        };
        unfold(panel.body.item);
        tryCompare(panel.bodyView, "contentHeight", panel.body.implicitHeight);
        const tall = panel.bodyView.contentHeight;
        // Closed in between, as the canvas closes one block's settings
        // before it opens the next.
        canvas.closeSettings();
        canvas.openSettings(shortBlock);
        tryVerify(() => panel.body.item !== null && panel.body.item.blockId === shortBlock, 1000);
        wait(50);
        tryCompare(panel.bodyView, "contentHeight", panel.body.implicitHeight);
        verify(panel.bodyView.contentHeight < tall, "the short panel kept the tall one's height");
        tryCompare(panel.bodyView.QQC2.ScrollBar.vertical, "size", 1);
    }

    // **The application is chosen from a list**, and says so: the field is
    // a list that drops down, in the style's frame with the style's arrow,
    // and the list it drops is the picker.
    function test_theApplicationIsChosenFromAList() {
        panel.open("app");
        tryVerify(() => panel.body.item !== null && panel.body.item.sourceButton !== undefined, 1000);
        const source = panel.body.item.sourceButton;
        compare(source.popup, panel.body.item.picker);
        verify(source.delegateModel !== undefined, "the source is not a list that drops down");
        compare(source.displayText, panel.body.item.sourceName);
        verify(source.indicator !== null, "the list has no arrow of the style's");
        // **The icon is a hint the style draws**, beside the one name the
        // style writes: the desktop style draws a list's icon from it, and
        // so does ours.
        compare(source.Kirigami.StyleHints.iconName, panel.body.item.sourceIcon);
        if (Appearance.ownStyle) {
            compare(source.hintedIcon, panel.body.item.sourceIcon);
            compare(source.leftPadding, AppMetrics.comboPadding + AppMetrics.buttonIconSize + source.spacing);
            compare(source.contentItem.text, panel.body.item.sourceName);
        }
        source.popup.open();
        tryVerify(() => panel.body.item.picker.opened, 1000, "the picker did not open");
        verify(source.down, "the list does not look open while it is");
        source.popup.close();
    }
}

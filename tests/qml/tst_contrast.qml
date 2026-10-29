// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **What the window writes and draws can be read**: WCAG AA, 4.5:1
// for words and 3:1 for a mark that has to be seen to be understood (a state
// dot, a live wire, a meter's bar, a focus ring), against every surface the
// window paints, in the dark and the light Reverblabr appearance and in
// *Follow system* under whatever scheme this process runs with. Breeze's
// two schemes are then checked by their colours, as tst_appcolors.qml does,
// because the process only ever has one of them.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "Contrast"

    readonly property var textRoles: ["text", "muted", "accentText", "liveText", "okText", "warningText", "dangerText"]
    readonly property var markRoles: ["accentMark", "liveMark", "okMark", "warningMark", "dangerMark"]
    readonly property var backgroundRoles: ["window", "canvas", "surface", "surface2", "field", "well"]

    function init() {
        Appearance.mode = Appearance.ReverblabrDark;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    function appearances() {
        return [
            { tag: "dark", mode: Appearance.ReverblabrDark },
            { tag: "light", mode: Appearance.ReverblabrLight }
        ];
    }

    /// Every pair of \a roles on every background that falls short of \a ratio.
    function shortfalls(roles, ratio) {
        const found = [];
        for (const role of roles) {
            for (const background of testCase.backgroundRoles) {
                const ratioFound = AppColors.contrast(AppColors[role], AppColors[background]);
                if (ratioFound < ratio) {
                    found.push(role + " on " + background + ": " + ratioFound.toFixed(2) + ":1");
                }
            }
        }
        return found;
    }

    function test_wordsReadAtFourAndAHalfToOneOnEverySurface_data() { return testCase.appearances(); }
    function test_wordsReadAtFourAndAHalfToOneOnEverySurface(data) {
        Appearance.mode = data.mode;
        const found = testCase.shortfalls(testCase.textRoles, 4.5);
        compare(found.length, 0, data.tag + ":\n" + found.join("\n"));
    }

    function test_marksReadAtThreeToOneOnEverySurface_data() { return testCase.appearances(); }
    function test_marksReadAtThreeToOneOnEverySurface(data) {
        Appearance.mode = data.mode;
        const found = testCase.shortfalls(testCase.markRoles, 3.0);
        compare(found.length, 0, data.tag + ":\n" + found.join("\n"));
    }

    // **A control that is only an outline can be seen to be there**:
    // a text field's frame, a check box's box, a button with no fill, on
    // every surface a control stands on (WCAG 1.4.11).
    function test_aControlsOutlineReadsAtThreeToOne_data() { return testCase.appearances(); }
    function test_aControlsOutlineReadsAtThreeToOne(data) {
        Appearance.mode = data.mode;
        const found = [];
        for (const background of ["window", "surface", "surface2", "field"]) {
            const ratio = AppColors.contrast(AppColors.controlBorder, AppColors[background]);
            if (ratio < 3.0) {
                found.push("controlBorder on " + background + ": " + ratio.toFixed(2) + ":1");
            }
        }
        compare(found.length, 0, data.tag + ":\n" + found.join("\n"));
    }

    function test_anIdleWireCanBeSeenOnTheCanvas_data() { return testCase.appearances(); }
    function test_anIdleWireCanBeSeenOnTheCanvas(data) {
        Appearance.mode = data.mode;
        const ratio = AppColors.contrast(AppColors.wireMark, AppColors.canvas);
        verify(ratio >= 3.0, data.tag + ": an idle wire is only " + ratio.toFixed(2) + ":1 on the canvas");
    }

    // **Writing on a fill**: a filled button's label on the accent, and the
    // REC pill's time on its red, in the Reverblabr appearance, whose colours
    // are ours. In *Follow system* the label on the accent is the scheme's
    // own pairing, and left to it.
    function test_labelsOnAFillCanBeRead_data() {
        return [
            { tag: "dark", mode: Appearance.ReverblabrDark },
            { tag: "light", mode: Appearance.ReverblabrLight }
        ];
    }

    function test_labelsOnAFillCanBeRead(data) {
        Appearance.mode = data.mode;
        const onAccent = AppColors.contrast(AppColors.textOnAccent, AppColors.accent);
        verify(onAccent >= 4.5, data.tag + ": a label on the accent is only " + onAccent.toFixed(2) + ":1");
        const onRed = AppColors.contrast(AppColors.inkOnDanger, AppColors.dangerFill);
        verify(onRed >= 4.5, data.tag + ": the REC pill's time is only " + onRed.toFixed(2) + ":1");
    }

    // **What sits on a control's own fill**: a switch's knob on its
    // track, off and on, and the close button's cross on its red, all marks.
    function test_marksOnAControlsFillCanBeSeen_data() { return testCase.appearances(); }
    function test_marksOnAControlsFillCanBeSeen(data) {
        Appearance.mode = data.mode;
        const off = AppColors.contrast(AppColors.knob, AppColors.well);
        verify(off >= 3.0, data.tag + ": an off switch's knob is only " + off.toFixed(2) + ":1 on its track");
        const on = AppColors.contrast(AppColors.textOnAccent, AppColors.accent);
        verify(on >= 3.0, data.tag + ": an on switch's knob is only " + on.toFixed(2) + ":1 on its track");
        const cross = AppColors.contrast(AppColors.inkOnDanger, AppColors.danger);
        verify(cross >= 3.0, data.tag + ": the close button's cross is only " + cross.toFixed(2) + ":1 on its red");
    }

    // **Nothing is moved that did not have to be**: a colour that already
    // clears its ratio is the fill itself.
    function test_whatAlreadyReadsIsLeftAlone() {
        Appearance.mode = Appearance.ReverblabrDark;
        compare(AppColors.liveText, AppColors.live, "the dark live orange was moved though it reads");
        compare(AppColors.okMark, AppColors.ok, "the dark ok green was moved though it reads");
        compare(AppColors.text.toString(), AppColors.readableOn(AppColors.text, 4.5, AppColors.textBackgrounds).toString());
    }

    // **Secondary text reads in *Follow system* too**: under the
    // scheme this process runs with, the text and the secondary text clear
    // 4.5:1 on every surface. In the Basic suite that is Kirigami's own
    // palette, whose disabled-text colour, the old secondary text, read at
    // 1.62:1 on the window and 1.23:1 on a well; the style suite gives
    // Kirigami the design's colours. The other roles in *Follow system* are
    // the scheme's own and left to it.
    function test_followingTheSystemSecondaryTextReads() {
        Appearance.mode = Appearance.FollowSystem;
        const found = testCase.shortfalls(["text", "muted"], 4.5);
        compare(found.length, 0, "follow system:\n" + found.join("\n"));
        verify(AppColors.contrast(AppColors.muted, AppColors.window) < AppColors.contrast(AppColors.text, AppColors.window),
               "the secondary text is the text colour itself, not a step below it");
    }

    // **And under both of Breeze's schemes**, by their colours, since the
    // process only ever has one of them: the Window set's background,
    // alternate background and text, with the two depths AppColors makes
    // from them. The secondary text is what AppColors.secondaryToward()
    // makes of them, so what is pinned is the decision, not a copy of it.
    function breezeSchemes() {
        return [
            { tag: "breeze-light", window: "#eff0f1", alternate: "#e3e5e7", text: "#232629", expected: "#575c61" },
            { tag: "breeze-dark", window: "#202326", alternate: "#292c30", text: "#fcfcfc", expected: "#a0a6ab" }
        ];
    }

    function test_secondaryTextUnderBreezeReads_data() { return testCase.breezeSchemes(); }
    function test_secondaryTextUnderBreezeReads(data) {
        const surfaces = {
            "window": data.window,
            "canvas": data.alternate,
            "surface": data.window,
            "surface2": Kirigami.ColorUtils.linearInterpolation(data.window, data.text, AppColors.raisedFraction),
            "field": data.window,
            "well": Kirigami.ColorUtils.linearInterpolation(data.window, data.text, AppColors.sunkenFraction)
        };
        const backgrounds = Object.keys(surfaces).map(name => surfaces[name]);
        const secondary = AppColors.secondaryToward(data.text, data.window, 4.5, backgrounds);
        const found = [];
        for (const name of Object.keys(surfaces)) {
            const ratio = AppColors.contrast(secondary, surfaces[name]);
            if (ratio < 4.5) {
                found.push("secondary on " + name + ": " + ratio.toFixed(2) + ":1");
            }
        }
        compare(found.length, 0, data.tag + ":\n" + found.join("\n"));
        verify(AppColors.contrast(secondary, data.window) < AppColors.contrast(data.text, data.window),
               data.tag + ": the secondary text is the text colour itself, not a step below it");
        compare(secondary.toString(), data.expected, data.tag + ": a different secondary text");
    }

    // **Following the system leaves the scheme's colours alone**: the words
    // and marks are the scheme's roles themselves.
    function test_followingTheSystemLeavesTheSchemesColoursAlone() {
        Appearance.mode = Appearance.FollowSystem;
        for (const [made, from] of [["accentText", "accent"], ["liveText", "live"], ["okText", "ok"],
                                    ["warningText", "warning"], ["dangerText", "danger"], ["accentMark", "accent"],
                                    ["liveMark", "live"], ["okMark", "ok"], ["warningMark", "warning"],
                                    ["dangerMark", "danger"], ["wireMark", "wire"], ["dangerFill", "danger"],
                                    ["knob", "textOnAccent"], ["controlBorder", "border"]]) {
            compare(AppColors[made], AppColors[from], made + " is not the scheme's " + from);
        }
    }
}

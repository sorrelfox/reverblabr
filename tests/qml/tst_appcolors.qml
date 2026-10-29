// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The two depths a colour scheme has no role for: a surface raised
// above the one it sits on, and a trough cut into it.
//
// **Measured, not judged by eye**, the way the category tints already are
// (tst_blockpresentation.qml) — and measured on *both* of Breeze's schemes,
// because a step that is obvious on a dark one can vanish on a light one and
// the test process only ever has one of them.

import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "AppColors"

    // Breeze's window background and text, which is what AppColors.surface
    // and AppColors.text resolve to under each scheme.
    readonly property color darkSurface: "#2a2e32"
    readonly property color darkText: "#eff0f1"
    readonly property color lightSurface: "#eff0f1"
    readonly property color lightText: "#232627"

    // What the design's own two palettes step by, measured the same way:
    // raised is 1.10 to 1.13, sunken is 1.29 to 1.31. Ours has to land in
    // the same country without having to be the same number, because the
    // scheme it starts from is the user's and not the design's.
    readonly property real minimumRaised: 1.07
    readonly property real maximumRaised: 1.25
    readonly property real minimumSunken: 1.20
    readonly property real maximumSunken: 1.55

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

    /// The roles as AppColors builds them, for a scheme that is not the one
    /// this process is running under. **The fractions come from AppColors**,
    /// so what is pinned here is the decision and not a copy of it.
    function raisedFrom(surface, text) {
        return Kirigami.ColorUtils.linearInterpolation(surface, text, AppColors.raisedFraction);
    }

    function sunkenFrom(surface, text) {
        return Kirigami.ColorUtils.linearInterpolation(surface, text, AppColors.sunkenFraction);
    }

    /// Both of Breeze's schemes, for every test below. Qt Quick Test wants
    /// one `<name>_data()` per test, so they all hand back this.
    function schemes() {
        return [
            { tag: "dark", surface: testCase.darkSurface, text: testCase.darkText },
            { tag: "light", surface: testCase.lightSurface, text: testCase.lightText }
        ];
    }

    function test_raised_is_a_visible_step_data() { return testCase.schemes(); }
    function test_sunken_is_a_visible_step_data() { return testCase.schemes(); }
    function test_a_trough_is_deeper_than_a_raise_is_high_data() { return testCase.schemes(); }
    function test_both_steps_go_toward_the_text_data() { return testCase.schemes(); }
    function test_text_stays_readable_on_both_data() { return testCase.schemes(); }

    // **A step nobody can see is not a step.** This is the whole reason the
    // roles exist: `canvas`, which everything used to reach for, is one unit
    // from `surface` on Breeze's dark scheme.
    function test_raised_is_a_visible_step(data) {
        const raised = testCase.raisedFrom(data.surface, data.text);
        const ratio = testCase.contrast(raised, data.surface);
        verify(ratio >= testCase.minimumRaised,
               data.tag + ": a raised surface is " + ratio.toFixed(3)
               + " from the surface it sits on, which is under " + testCase.minimumRaised);
        verify(ratio <= testCase.maximumRaised,
               data.tag + ": a raised surface is " + ratio.toFixed(3)
               + " from the surface it sits on, which reads as a different colour rather than a step");
    }

    function test_sunken_is_a_visible_step(data) {
        const sunken = testCase.sunkenFrom(data.surface, data.text);
        const ratio = testCase.contrast(sunken, data.surface);
        verify(ratio >= testCase.minimumSunken,
               data.tag + ": a trough is " + ratio.toFixed(3) + " from its surface, which is under "
               + testCase.minimumSunken);
        verify(ratio <= testCase.maximumSunken,
               data.tag + ": a trough is " + ratio.toFixed(3) + " from its surface, which is a hole rather than a well");
    }

    // A trough is cut deeper than a raise is raised, on every scheme, or the
    // two roles would be interchangeable and nothing would be gained by
    // having both.
    function test_a_trough_is_deeper_than_a_raise_is_high(data) {
        const raised = testCase.contrast(testCase.raisedFrom(data.surface, data.text), data.surface);
        const sunken = testCase.contrast(testCase.sunkenFrom(data.surface, data.text), data.surface);
        verify(sunken > raised,
               data.tag + ": trough " + sunken.toFixed(3) + " is not deeper than raise " + raised.toFixed(3));
    }

    // **Both steps go the same way**, toward the text colour, which is the
    // only direction certain to exist: a black background has nowhere darker
    // to go and a white one nowhere lighter.
    function test_both_steps_go_toward_the_text(data) {
        const surface = testCase.luminance(data.surface);
        const text = testCase.luminance(data.text);
        const raised = testCase.luminance(testCase.raisedFrom(data.surface, data.text));
        const sunken = testCase.luminance(testCase.sunkenFrom(data.surface, data.text));
        const towardText = (value) => text > surface ? value > surface : value < surface;
        verify(towardText(raised), data.tag + ": a raised surface went away from the text colour");
        verify(towardText(sunken), data.tag + ": a trough went away from the text colour");
    }

    // Both carry text — the panel header's name, a readout's labels, the
    // numbers beside a meter — so both have to keep it readable.
    function test_text_stays_readable_on_both(data) {
        const onRaised = testCase.contrast(data.text, testCase.raisedFrom(data.surface, data.text));
        const onSunken = testCase.contrast(data.text, testCase.sunkenFrom(data.surface, data.text));
        verify(onRaised >= 4.5, data.tag + ": text on a raised surface is only " + onRaised.toFixed(2) + ":1");
        verify(onSunken >= 4.5, data.tag + ": text in a trough is only " + onSunken.toFixed(2) + ":1");
    }

    // The roles the window actually uses, under whatever scheme this process
    // is running: they must be the same two steps, and neither may collapse
    // onto the surface it is a step from.
    function test_the_live_scheme_gets_both_steps() {
        const raised = testCase.contrast(AppColors.surface2, AppColors.surface);
        const sunken = testCase.contrast(AppColors.well, AppColors.surface);
        verify(raised >= testCase.minimumRaised, "surface2 is only " + raised.toFixed(3) + " from surface");
        verify(sunken >= testCase.minimumSunken, "well is only " + sunken.toFixed(3) + " from surface");
        verify(sunken > raised, "well is not deeper than surface2 is raised");
    }

    // **What is written on the live colour** — Stop, on the transport
    // button. The design puts white there, which is about 2:1 on
    // either orange; the label is ordinary-sized text, so it needs 4.5:1,
    // on both of them.
    function test_writing_on_the_live_colour_is_legible_data() {
        return [
            { tag: "dark canvas", live: AppColors.liveOnDarkCanvas },
            { tag: "light canvas", live: AppColors.liveOnLightCanvas }
        ];
    }

    function test_writing_on_the_live_colour_is_legible(data) {
        const ratio = testCase.contrast(AppColors.inkOnLive, data.live);
        verify(ratio >= 4.5, data.tag + ": writing on the live colour is only " + ratio.toFixed(2) + ":1");
    }

    // **The ears' badge**: the letter L or R, white on its ear's
    // colour. Ordinary-sized text, so 4.5:1, and the two colours told
    // apart by more than their hue — the letter is there for that.
    function test_the_ear_letters_are_legible_data() {
        return [
            { tag: "left", ear: AppColors.earLeft, ratio: 4.72 },
            { tag: "right", ear: AppColors.earRight, ratio: 4.77 }
        ];
    }

    function test_the_ear_letters_are_legible(data) {
        const ratio = testCase.contrast(AppColors.inkOnEar, data.ear);
        verify(ratio >= 4.5, data.tag + ": the letter on the " + data.tag + " ear's badge is only "
               + ratio.toFixed(2) + ":1");
        fuzzyCompare(ratio, data.ratio, 0.01, data.tag + " has moved to " + ratio.toFixed(2) + ":1");
    }

    // --- the Reverblabr appearance ---

    // **Every colour, as the prototype has it**, copied here by hand from
    // the stylesheet of the prototype's main window rather than read from it:
    // the prototype is not in the repository, and a test that read the
    // palette from AppColors would only prove AppColors agrees with itself.
    // The token's name, then the prototype's name for it in a comment where
    // they differ.
    readonly property var prototypeDark: ({
            "window": "#1b1e20", // --bg
            "canvas": "#212427",
            "surface": "#2a2e32",
            "surface2": "#31363b",
            "field": "#232629",
            "well": "#3b4045",
            "border": "#3f454b",
            "text": "#eff0f1",
            "muted": "#9aa3ab",
            "accent": "#3daee9",
            "accentSoft": Qt.rgba(61 / 255, 174 / 255, 233 / 255, 0.35), // rgba(61,174,233,.35)
            "live": "#f5a623",
            "ok": "#2ecc71",
            "danger": "#da4453",
            "wire": "#4d555c",
            "grid": Qt.rgba(1, 1, 1, 0.055), // rgba(255,255,255,.055)
            "textOnAccent": "#ffffff", // the Run button's label
            "shadow": Qt.rgba(0, 0, 0, 0.3), // a docked panel: 0 8px 24px rgba(0,0,0,.3)
            "liftedShadow": Qt.rgba(0, 0, 0, 0.45), // a floating one: 0 16px 40px rgba(0,0,0,.45)
            "linkHover": "#93cee9", // a:hover
            "meterHot": "#ffd27a", // the top of a meter's gradient
            "flowDash": Qt.rgba(1, 1, 1, 0.55) // a live wire's dashes: rgba(255,255,255,.55)
        })
    readonly property var prototypeLight: ({
            "window": "#eff0f1",
            "canvas": "#fafbfb",
            "surface": "#ffffff",
            "surface2": "#f3f4f5",
            "field": "#ffffff",
            "well": "#dfe3e6",
            "border": "#c8ced3",
            "text": "#232627",
            "muted": "#6b7378",
            "accent": "#2a97d0",
            "accentSoft": Qt.rgba(42 / 255, 151 / 255, 208 / 255, 0.3),
            "live": "#e08a00",
            "ok": "#27ae60",
            "danger": "#da4453",
            "wire": "#b7bec4",
            "grid": Qt.rgba(0, 0, 0, 0.07),
            "textOnAccent": "#ffffff",
            "shadow": Qt.rgba(0, 0, 0, 0.3),
            "liftedShadow": Qt.rgba(0, 0, 0, 0.45),
            "linkHover": "#93cee9",
            "meterHot": "#ffd27a",
            "flowDash": Qt.rgba(1, 1, 1, 0.55)
        })

    // Each test says which appearance it is about, and the suite goes
    // back to the application's default after it.
    function init() {
        Appearance.mode = Appearance.ReverblabrDark;
    }

    function cleanup() {
        Appearance.mode = SuiteAppearance;
    }

    function test_every_colour_is_the_prototypes_data() {
        return [
            { tag: "dark", mode: Appearance.ReverblabrDark, expected: testCase.prototypeDark },
            { tag: "light", mode: Appearance.ReverblabrLight, expected: testCase.prototypeLight }
        ];
    }

    /// **The colours that depart from the prototype for somebody to read
    /// them**: its muted grey, moved only as far as 4.5:1 on every
    /// surface, and the label on its accent, near-black where white is 2.5:1
    /// and 3.3:1. tst_contrast.qml holds both to their ratios instead, and
    /// test_the_colours_that_are_not_the_prototypes() below pins them.
    readonly property var readableNotPrototype: ["muted", "textOnAccent"]

    function test_every_colour_is_the_prototypes(data) {
        Appearance.mode = data.mode;
        for (const name of Object.keys(data.expected)) {
            if (testCase.readableNotPrototype.indexOf(name) >= 0) {
                continue;
            }
            const actual = AppColors[name];
            const expected = Qt.color(data.expected[name]);
            verify(actual !== undefined, data.tag + ": AppColors has no " + name);
            // Channel by channel, to the nearest of 255 steps: an alpha of
            // .055 is not a whole number of them, and 8-bit is what is drawn.
            const near = (a, b) => Math.abs(a - b) <= 0.5 / 255;
            verify(near(actual.r, expected.r) && near(actual.g, expected.g) && near(actual.b, expected.b)
                   && near(actual.a, expected.a),
                   data.tag + ": " + name + " is " + actual + ", the prototype's is " + expected);
        }
    }

    // **The live colour is the prototype's, under either name.** The two
    // oranges are its two palettes' `--live`, so the canvas's own darkness
    // picks between them in every appearance.
    function test_the_live_colours_are_the_prototypes() {
        compare(AppColors.liveOnDarkCanvas, Qt.color("#f5a623"));
        compare(AppColors.liveOnLightCanvas, Qt.color("#e08a00"));
        Appearance.mode = Appearance.ReverblabrLight;
        compare(AppColors.live, Qt.color("#e08a00"));
        Appearance.mode = Appearance.ReverblabrDark;
        compare(AppColors.live, Qt.color("#f5a623"));
    }

    // **The warning is the one colour the prototype has none for**, and the
    // ink on the live colour the one it gets wrong; both are recorded in
    // ADR-0012, and pinned here so a change to either is a decision.
    function test_the_two_colours_that_are_not_the_prototypes() {
        compare(AppColors.warning, Qt.color("#f67400"));
        compare(AppColors.inkOnLive, Qt.color("#1b1e20"));
    }

    // **And the two that are not the prototype's so they can be read**:
    // the muted grey a step toward the text in the light palette
    // and a step in the dark one, and the label on the accent near-black in
    // both.
    function test_the_colours_that_are_not_the_prototypes_so_they_can_be_read_data() {
        return [
            { tag: "dark", mode: Appearance.ReverblabrDark, muted: "#a4acb3" },
            { tag: "light", mode: Appearance.ReverblabrLight, muted: "#5e6569" }
        ];
    }

    function test_the_colours_that_are_not_the_prototypes_so_they_can_be_read(data) {
        Appearance.mode = data.mode;
        compare(AppColors.muted.toString(), data.muted, data.tag + ": the muted grey has moved");
        compare(AppColors.textOnAccent, Qt.color("#1b1e20"), data.tag + ": the label on the accent is not near-black");
    }

    // **Following the system is the window before it had its own look**:
    // every name a colour-scheme role, and nothing of the prototype's.
    function test_following_the_system_is_the_schemes_roles() {
        Appearance.mode = Appearance.FollowSystem;
        compare(AppColors.followsSystem, true);
        const theme = testCase.Kirigami.Theme;
        compare(AppColors.window, theme.backgroundColor);
        compare(AppColors.canvas, theme.alternateBackgroundColor);
        compare(AppColors.surface, theme.backgroundColor);
        compare(AppColors.text, theme.textColor);
        // Secondary text: the scheme's text colour toward its background,
        // not its disabled-text colour.
        compare(AppColors.muted, AppColors.secondaryToward(theme.textColor, theme.backgroundColor, AppColors.textRatio,
                                                           AppColors.textBackgrounds));
        compare(AppColors.accent, theme.highlightColor);
        compare(AppColors.textOnAccent, theme.highlightedTextColor);
        compare(AppColors.ok, theme.positiveTextColor);
        compare(AppColors.warning, theme.neutralTextColor);
        compare(AppColors.danger, theme.negativeTextColor);
        compare(AppColors.surface2, testCase.raisedFrom(theme.backgroundColor, theme.textColor));
        compare(AppColors.well, testCase.sunkenFrom(theme.backgroundColor, theme.textColor));
        compare(AppColors.border, Kirigami.ColorUtils.linearInterpolation(theme.backgroundColor, theme.textColor, 0.2));
        compare(AppColors.wire, Kirigami.ColorUtils.linearInterpolation(theme.backgroundColor, theme.textColor, 0.3));
        compare(AppColors.meterHot, Kirigami.ColorUtils.linearInterpolation(AppColors.live, "#ffffff", 0.5));
    }

    // **Switching is live**: a binding to a name follows the choice with no
    // restart, which is what lets the window recolour as it is picked.
    function test_switching_changes_what_a_binding_sees() {
        const swatch = createTemporaryQmlObject(
            "import QtQuick; import io.github.reverblabr; Rectangle { color: AppColors.surface }", testCase);
        compare(swatch.color, Qt.color(testCase.prototypeDark.surface));
        Appearance.mode = Appearance.ReverblabrLight;
        compare(swatch.color, Qt.color(testCase.prototypeLight.surface));
        Appearance.mode = Appearance.FollowSystem;
        compare(swatch.color, testCase.Kirigami.Theme.backgroundColor);
    }
}

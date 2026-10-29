// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami

// The colours the interface draws with, under the names the design gives
// them ([ADR-0012]).
//
// **Two sources for one set of names.** In the Reverblabr appearance every
// name is the design prototype's own value, from one of its two palettes.
// In *Follow system* the same names are colour-scheme roles, exactly as they
// were before the application had a look of its own: the user's scheme and
// accent, for anyone who needs high contrast, another theme, or simply their
// own desktop. Nothing outside this file names a colour, so switching is one
// property changing and every binding following it.
QtObject {
    id: colors

    /// Whether the desktop's colour scheme decides, rather than the
    /// prototype's palettes.
    readonly property bool followsSystem: Appearance.followsSystem

    // **The prototype's two palettes**, as its stylesheet has them. The
    // names are its custom properties' (`--bg` is `bg`, `--accent-soft` is
    // `accentSoft`); the last five are colours it writes inline rather than
    // as properties — the Run button's label, the two panel shadows, a link
    // under the pointer, the hot end of a meter's bar and the dashes along
    // a live wire.
    // tst_appcolors.qml holds every one of them to the prototype.
    readonly property var darkPalette: ({
            "bg": "#1b1e20",
            "canvas": "#212427",
            "surface": "#2a2e32",
            "surface2": "#31363b",
            "field": "#232629",
            "well": "#3b4045",
            "border": "#3f454b",
            "text": "#eff0f1",
            "muted": "#9aa3ab",
            "accent": "#3daee9",
            "accentSoft": Qt.rgba(61 / 255, 174 / 255, 233 / 255, 0.35),
            "live": "#f5a623",
            "ok": "#2ecc71",
            "danger": "#da4453",
            "wire": "#4d555c",
            "grid": Qt.rgba(1, 1, 1, 0.055),
            "textOnAccent": "#ffffff",
            "shadow": Qt.rgba(0, 0, 0, 0.3),
            "liftedShadow": Qt.rgba(0, 0, 0, 0.45),
            "linkHover": "#93cee9",
            "meterHot": "#ffd27a",
            "flowDash": Qt.rgba(1, 1, 1, 0.55)
        })
    readonly property var lightPalette: ({
            "bg": "#eff0f1",
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

    /// The palette in force while not following the system.
    readonly property var designPalette: Appearance.dark ? colors.darkPalette : colors.lightPalette

    // **The warning colour is the one the prototype does not have.** It
    // never draws a warning; the window does (a device that has gone, a
    // session that could not be saved). Breeze's own, which is what the
    // default scheme gave it before the application had a look of its
    // own, and which sits between the live orange and the danger red on
    // both palettes.
    readonly property color designWarning: "#f67400"

    // The three depths the design uses, back to front: the window behind
    // everything, the canvas it sits on, and the tiles on the canvas.
    readonly property color window: colors.followsSystem ? Kirigami.Theme.backgroundColor
                                                         : colors.designPalette.bg
    readonly property color canvas: colors.followsSystem ? Kirigami.Theme.alternateBackgroundColor
                                                         : colors.designPalette.canvas
    readonly property color surface: colors.followsSystem ? Kirigami.Theme.backgroundColor
                                                          : colors.designPalette.surface
    /// Where something is typed or picked from: a text field, a list.
    readonly property color field: colors.followsSystem ? Kirigami.Theme.backgroundColor
                                                        : colors.designPalette.field

    readonly property color text: colors.followsSystem ? Kirigami.Theme.textColor : colors.designPalette.text
    /// Words that matter less than the ones around them: a unit, a hint, a
    /// caption. **In the Reverblabr appearance, readable, not faint**:
    /// the prototype's grey moved toward the text colour as far as
    /// it takes to read at 4.5:1 on every surface the window paints, and no
    /// further — the dark one a step, the light one from #6b7378 to #5e6569.
    /// **In *Follow system*, the scheme's text colour taken toward its
    /// background**: as far as it still reads at 4.5:1 on every
    /// surface, and no further — #575c61 under Breeze Light, #a0a6ab under
    /// Breeze Dark. Still the scheme's colours, but not its disabled-text
    /// role, which it gave before and which is about 2.4:1 under Breeze
    /// Light: secondary words are not a disabled control ([ADR-0012]).
    readonly property color muted: colors.followsSystem
                                   ? colors.secondaryToward(colors.text, colors.window, colors.textRatio,
                                                            colors.textBackgrounds)
                                   : colors.readableOn(colors.designPalette.muted, colors.textRatio,
                                                       colors.textBackgrounds)
    readonly property color accent: colors.followsSystem ? Kirigami.Theme.highlightColor
                                                         : colors.designPalette.accent
    /// The accent, faint: a selection's fill, a focus ring's glow.
    readonly property color accentSoft: colors.followsSystem
                                        ? Qt.rgba(colors.accent.r, colors.accent.g, colors.accent.b, 0.35)
                                        : colors.designPalette.accentSoft
    /// What is written on the accent: a filled button's label, a selection,
    /// a check mark. **The scheme's own pairing while following the system;
    /// in the Reverblabr appearance whichever of the prototype's white and a
    /// near-black reads better on the accent**. White on either blue
    /// is 2.5:1 and 3.3:1, under the 4.5:1 a label needs; near-black is over
    /// 5:1 on both. A departure from the prototype on purpose ([ADR-0012]),
    /// as the writing on the live colour is.
    readonly property color textOnAccent: colors.followsSystem ? Kirigami.Theme.highlightedTextColor
                                                           : colors.inkOn(colors.accent)
    readonly property color ok: colors.followsSystem ? Kirigami.Theme.positiveTextColor
                                                     : colors.designPalette.ok
    readonly property color warning: colors.followsSystem ? Kirigami.Theme.neutralTextColor
                                                          : colors.designWarning
    readonly property color danger: colors.followsSystem ? Kirigami.Theme.negativeTextColor
                                                         : colors.designPalette.danger
    // --- words and marks everybody can read ---
    //
    // **The colours above are fills; these are for what is drawn with
    // them.** The accent, the live orange, the ok green, the warning orange
    // and the danger red are the design's, and a panel's readout written in
    // the light accent, or a tile's state dot in the light live orange on a
    // white tile, falls short of what WCAG AA asks: 4.5:1 for words, 3:1
    // for a mark that has to be seen to be understood (a state dot, a live
    // wire, a meter's bar, a focus ring). So each has a colour for words and
    // one for marks, made from it by readableOn(): moved toward the text
    // colour as little as it takes to clear the ratio against every surface
    // the window paints. Where the fill already clears it, it is the fill.
    // tst_contrast.qml holds every one of them to its ratio in both
    // Reverblabr appearances.
    //
    // **Not while following the system** ([ADR-0012]): there the scheme is
    // the person's choice — a high-contrast one, if that is what they need —
    // and every one of these is the scheme's own colour, untouched.

    readonly property real textRatio: 4.5
    readonly property real markRatio: 3.0
    /// Every surface words and marks are drawn on.
    readonly property var textBackgrounds: [colors.window, colors.canvas, colors.surface, colors.surface2,
                                           colors.field, colors.well]

    readonly property color accentText: colors.readableOn(colors.accent, colors.textRatio, colors.textBackgrounds)
    readonly property color liveText: colors.readableOn(colors.live, colors.textRatio, colors.textBackgrounds)
    readonly property color okText: colors.readableOn(colors.ok, colors.textRatio, colors.textBackgrounds)
    readonly property color warningText: colors.readableOn(colors.warning, colors.textRatio, colors.textBackgrounds)
    readonly property color dangerText: colors.readableOn(colors.danger, colors.textRatio, colors.textBackgrounds)

    readonly property color accentMark: colors.readableOn(colors.accent, colors.markRatio, colors.textBackgrounds)
    readonly property color liveMark: colors.readableOn(colors.live, colors.markRatio, colors.textBackgrounds)
    readonly property color okMark: colors.readableOn(colors.ok, colors.markRatio, colors.textBackgrounds)
    readonly property color warningMark: colors.readableOn(colors.warning, colors.markRatio, colors.textBackgrounds)
    readonly property color dangerMark: colors.readableOn(colors.danger, colors.markRatio, colors.textBackgrounds)
    /// A wire with nothing going through it, which is still a wire: it says
    /// what is connected to what. Only ever drawn on the canvas.
    readonly property color wireMark: colors.readableOn(colors.wire, colors.markRatio, [colors.canvas])
    /// **The outline of a control that is only an outline**: a text field,
    /// a check box, a radio button, a segmented choice, a button with no
    /// fill. WCAG 1.4.11 asks 3:1 for what tells somebody a control is
    /// there; the plain border, 1.6:1 on a field, stays for the
    /// lines that only divide, which would be heavy at that weight.
    readonly property color controlBorder: colors.readableOn(colors.border, colors.markRatio, colors.controlBackgrounds)
    /// Every surface a control's outline is drawn on or against.
    readonly property var controlBackgrounds: [colors.window, colors.surface, colors.surface2, colors.field]
    /// A fill with words on it in white, as the REC pill's red: the danger
    /// red, taken toward black until white clears 4.5:1 on it.
    readonly property color dangerFill: colors.followsSystem ? colors.danger
                                                             : colors.fillUnder(colors.danger, colors.inkOnDanger,
                                                                                colors.textRatio)
    readonly property color inkOnDanger: "#ffffff"
    /// A switch's knob while it is off, on the well of its track: the
    /// prototype's white, where that reads, and the near-black where it
    /// does not (the light track, 1.3:1 in white). On the accent, once it is
    /// on, the knob is whatever is written on the accent.
    readonly property color knob: colors.followsSystem ? colors.textOnAccent : colors.inkOn(colors.well)

    /// One channel of a colour, undone back to light (WCAG 2.1).
    function linear(channel: real): real {
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
    }

    /// How much light a colour gives back, 0 for black and 1 for white.
    function luminance(colour: color): real {
        return 0.2126 * colors.linear(colour.r) + 0.7152 * colors.linear(colour.g) + 0.0722 * colors.linear(colour.b);
    }

    /// The contrast ratio between two colours, from 1 to 21.
    function contrast(first: color, second: color): real {
        const a = colors.luminance(first);
        const b = colors.luminance(second);
        return (Math.max(a, b) + 0.05) / (Math.min(a, b) + 0.05);
    }

    /// \a colour, moved toward the text colour in steps of 2 % until it
    /// reads at \a ratio on every one of \a backgrounds; the text colour
    /// itself when nothing short of it does.
    function readableOn(colour: color, ratio: real, backgrounds: var): color {
        return colors.followsSystem ? colour : colors.readableToward(colour, ratio, backgrounds, colors.text);
    }

    /// The same, toward \a toward: another scheme's text colour, for a test
    /// that measures one this process is not running.
    function readableToward(colour: color, ratio: real, backgrounds: var, toward: color): color {
        // **The colour itself when it already reads**, not a copy of it: a
        // test, and anything else, can compare it with the fill.
        if (backgrounds.every(background => colors.contrast(colour, background) >= ratio)) {
            return colour;
        }
        for (let step = 1; step <= 50; ++step) {
            const tried = colors.whole(Kirigami.ColorUtils.linearInterpolation(colour, toward, step / 50));
            if (backgrounds.every(background => colors.contrast(tried, background) >= ratio)) {
                return tried;
            }
        }
        return toward;
    }

    /// **Words that matter less, from a scheme's own two colours**:
    /// \a text taken toward \a background in steps of 2 % for as long as
    /// it still reads at \a ratio on every one of \a backgrounds, and no
    /// further; \a text itself when no step short of it does. The faintest
    /// that can be read, so it stays a step below the text.
    function secondaryToward(text: color, background: color, ratio: real, backgrounds: var): color {
        for (let step = 50; step > 0; --step) {
            const tried = colors.whole(Kirigami.ColorUtils.linearInterpolation(text, background, step / 50));
            if (backgrounds.every(surface => colors.contrast(tried, surface) >= ratio)) {
                return tried;
            }
        }
        return text;
    }

    /// \a colour to the nearest of 255 steps a channel, which is what is
    /// drawn and what a palette holds: a colour made here then compares
    /// equal to itself after going through one.
    function whole(colour: color): color {
        return Qt.rgba(Math.round(colour.r * 255) / 255, Math.round(colour.g * 255) / 255,
                       Math.round(colour.b * 255) / 255, Math.round(colour.a * 255) / 255);
    }

    /// Whichever of white and near-black reads better written on \a fill.
    function inkOn(fill: color): color {
        return colors.contrast("#ffffff", fill) >= colors.contrast(colors.inkOnLive, fill) ? "#ffffff" : colors.inkOnLive;
    }

    /// \a fill, darkened (or lightened) toward the opposite of \a ink until
    /// \a ink reads at \a ratio on it.
    function fillUnder(fill: color, ink: color, ratio: real): color {
        const away = colors.luminance(ink) > 0.5 ? "#000000" : "#ffffff";
        for (let step = 0; step <= 50; ++step) {
            const tried = colors.whole(Kirigami.ColorUtils.linearInterpolation(fill, away, step / 50));
            if (colors.contrast(ink, tried) >= ratio) {
                return tried;
            }
        }
        return away;
    }

    /// A link under the pointer.
    readonly property color linkHover: colors.followsSystem ? Kirigami.Theme.hoverColor
                                                            : colors.designPalette.linkHover

    // **A step up from a surface, and a step down into one.** A panel's
    // header, a hovered tab and a readout floating over a plot sit *above*
    // the surface they are on; a meter's trough and the well a curve is
    // drawn in are cut *into* it. The prototype has a colour for each. No
    // colour scheme has a role for either, so in *Follow system* they are
    // made from the surface and the text.
    //
    // **Both steps go toward the text colour**: lighter on a dark scheme,
    // darker on a light one. That is the direction the prototype moves in
    // for both of them on both of its palettes, and it is the only direction
    // that is certain to exist — a scheme whose background is already black
    // has nowhere darker to go, and one whose background is white has
    // nowhere lighter.
    //
    // The two fractions are the whole of the decision, so they are named
    // rather than buried in the calls, and tst_appcolors.qml measures what
    // they come to on both of Breeze's schemes.
    readonly property real raisedFraction: 0.05
    readonly property real sunkenFraction: 0.12

    readonly property color surface2: colors.followsSystem
                                      ? Kirigami.ColorUtils.linearInterpolation(colors.surface, colors.text,
                                                                                colors.raisedFraction)
                                      : colors.designPalette.surface2
    readonly property color well: colors.followsSystem
                                  ? Kirigami.ColorUtils.linearInterpolation(colors.surface, colors.text,
                                                                            colors.sunkenFraction)
                                  : colors.designPalette.well

    // Breeze's own separator formula, so a line of ours sits at the same
    // weight as a line drawn by a widget beside it.
    readonly property color border: colors.followsSystem
                                    ? Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                              Kirigami.Theme.textColor, 0.2)
                                    : colors.designPalette.border
    readonly property color wire: colors.followsSystem
                                  ? Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor,
                                                                            Kirigami.Theme.textColor, 0.3)
                                  : colors.designPalette.wire
    readonly property color grid: colors.followsSystem
                                  ? Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.alternateBackgroundColor,
                                                                            Kirigami.Theme.textColor, 0.12)
                                  : colors.designPalette.grid

    // What a settings panel casts on the canvas under it, and what it casts
    // once it has been pulled off its block: heavier, so it reads as further
    // away. The same in every appearance — a shadow is a darkening, not a
    // colour.
    readonly property color shadow: colors.designPalette.shadow
    readonly property color liftedShadow: colors.designPalette.liftedShadow

    // **The one application colour**, for a wire with sound going through
    // it. It is not a scheme role because it does not mean "warning" or
    // "highlight"; it means "this is live", which is ours to say. Two of
    // them, because one orange cannot be read on both a light and a dark
    // canvas — the prototype's own two.
    readonly property color liveOnDarkCanvas: colors.darkPalette.live
    readonly property color liveOnLightCanvas: colors.lightPalette.live
    readonly property color live: Kirigami.ColorUtils.brightnessForColor(colors.canvas) === Kirigami.ColorUtils.Dark
                                  ? colors.liveOnDarkCanvas : colors.liveOnLightCanvas
    /// The hot end of a meter, where a level's peak is drawn: the
    /// prototype's pale yellow, or in *Follow system* the live colour
    /// lifted halfway to white, as it was before the application had a look
    /// of its own.
    readonly property color meterHot: colors.followsSystem
                                      ? Kirigami.ColorUtils.linearInterpolation(colors.live, "#ffffff", 0.5)
                                      : colors.designPalette.meterHot
    /// The pale dashes that run along a live wire in the prototype,
    /// over its orange. While following the system the wire's own orange is
    /// dashed instead, and this is not drawn.
    readonly property color flowDash: colors.designPalette.flowDash

    // **Writing on the live colour is dark, in every appearance.** The
    // prototype puts white on it, and white on either orange is about 2:1 —
    // under even the 3:1 that large text is allowed. Both oranges are light
    // enough that a near-black clears 4.5:1 on them, which tst_appcolors.qml
    // measures. One of the two places the Reverblabr appearance departs from
    // the prototype on purpose ([ADR-0012]): a label nobody can read is not
    // a match worth having.
    readonly property color inkOnLive: "#1b1e20"

    // **The two ears**, as an audiogram draws them: the left in blue,
    // the right in red. A block for one ear wears a badge of its ear's
    // colour with the letter on it, so the colour is never the only cue.
    // The same in every appearance, because they are a convention and not
    // part of any scheme; white on either clears 4.5:1, which
    // tst_appcolors.qml measures. Not the prototype's, which has no ears.
    readonly property color earLeft: "#2f6fde"
    readonly property color earRight: "#d23a3a"
    readonly property color inkOnEar: "#ffffff"

    // **The block categories.** A category's colour says "this is a source"
    // and "this is a meter"; no scheme has a role for that. They are hues,
    // not shades: the icon they tint is a single-colour mask, so the tint is
    // the only thing telling a person which kind of block they are looking
    // at.
    //
    // **The prototype's six**, one set for both of its palettes, are what
    // the dark appearance draws with; every one clears 3:1 on its canvas.
    // Hearing came after the prototype: a warm brown, clear of the
    // live orange and the danger red, in each table.
    readonly property var designTints: ({
            "sources": "#3daee9",
            "outputs": "#2ecc71",
            "effects": "#a06be0",
            "meters": "#1abc9c",
            "advanced": "#e05c8b",
            "plugins": "#7f8c8d",
            "hearing": "#d98a5b"
        })

    // **On a light canvas three of the prototype's hues fall to about
    // 2:1**, where an icon needs 3:1 to be told apart at all. So the light
    // appearance, and a light colour scheme, keep these: the prototype's
    // hue at whatever lightness clears 3:1, which tst_blockpresentation.qml
    // measures and writes down. The other place the Reverblabr appearance
    // departs from the prototype on purpose ([ADR-0012]).
    readonly property var tintsOnLightCanvas: ({
            "sources": "#1582bb",
            "outputs": "#208d4e",
            "effects": "#9b59b6",
            "meters": "#138a73",
            "advanced": "#d4418b",
            "plugins": "#707d7e",
            "hearing": "#a4532a"
        })
    // What a dark colour scheme gets in *Follow system*: Breeze's own hues,
    // each clearing 3:1 on Breeze's dark canvas.
    readonly property var tintsOnDarkCanvas: ({
            "sources": "#3daee9",
            "outputs": "#27ae60",
            "effects": "#a164bb",
            "meters": "#16a085",
            "advanced": "#da5b9b",
            "plugins": "#7f8c8d",
            "hearing": "#d9804f"
        })

    /// The table of tints for the canvas and the appearance of the moment.
    readonly property var tints: Kirigami.ColorUtils.brightnessForColor(colors.canvas) !== Kirigami.ColorUtils.Dark
                                 ? colors.tintsOnLightCanvas
                                 : colors.followsSystem ? colors.tintsOnDarkCanvas : colors.designTints

    // **The colours, for what is not QML**: under the Reverblabr
    // control style the application palette, which Qt's Basic controls
    // draw with wherever the style has none of its own yet, and Kirigami's
    // colours, which its platform plugin makes from these names. Nothing
    // while the system decides: the desktop's own palette comes back. The
    // appearance does nothing with them under the desktop style.
    readonly property var designColors: colors.followsSystem ? ({}) : ({
            "window": colors.window,
            "canvas": colors.canvas,
            "surface": colors.surface,
            "surface2": colors.surface2,
            "field": colors.field,
            "well": colors.well,
            "border": colors.border,
            "wire": colors.wire,
            "text": colors.text,
            "muted": colors.muted,
            "accent": colors.accent,
            "textOnAccent": colors.textOnAccent,
            "linkHover": colors.linkHover,
            "ok": colors.ok,
            "warning": colors.warning,
            "danger": colors.danger,
            "shadow": colors.shadow,
            "monoFamily": Appearance.monoFamily,
            // Not a colour, but carried the same way to the one place that
            // needs it: Kirigami's corner, for its dialogs and messages.
            "cornerRadius": AppMetrics.panelRadius
        })
    // **Handed over after the change, not during it**: a new palette has
    // Kirigami's colours change at once, and in *Follow system* some of the
    // colours above are Kirigami's — so doing it from inside the change
    // would be a binding loop.
    function handOver() {
        Appearance.setDesignColors(colors.designColors);
    }
    onDesignColorsChanged: Qt.callLater(colors.handOver)
    Component.onCompleted: colors.handOver()

    // The tint for a category, by the engine's stable category name. A name
    // this table has never heard of gets the muted text colour rather than
    // nothing at all, because an icon drawn in "undefined" is not drawn.
    function categoryTint(category: string): color {
        const tints = colors.tints;
        return tints[category] !== undefined ? tints[category] : colors.muted;
    }
}

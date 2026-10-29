// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.kirigami as Kirigami

// The sizes the interface is built to ([ADR-0012]): corners, gaps,
// paddings, the heights of rows, the size of a tile.
//
// **Two kinds of measure.** The corners, gaps and shadows are drawn by
// everything, today, so they switch with the appearance: the prototype's
// pixels in the Reverblabr appearance, Kirigami's units in *Follow system*,
// exactly what each place used before the application had a look of its
// own. The heights and widths are the prototype's layout — a 40-pixel
// header, a 132-by-96 tile — and hold its numbers in both; what the window
// does with them while following the system is decided where they are used,
// not here.
//
// All in logical pixels. tst_appmetrics.qml holds the Reverblabr values to
// the prototype.
QtObject {
    id: metrics

    readonly property bool followsSystem: Appearance.followsSystem

    // **Corners.** The prototype rounds each kind of thing by its own
    // amount: the least for the smallest things, the most for the window.
    /// A bar in a tile's little chart.
    readonly property real barRadius: metrics.followsSystem ? 1 : 1.5
    /// A meter's trough, a slider's groove.
    readonly property real troughRadius: 2
    /// A segmented choice, a check box, a key cap.
    readonly property real smallRadius: metrics.followsSystem ? Kirigami.Units.cornerRadius : 3
    /// A button, a field, a row, a tab.
    readonly property real controlRadius: metrics.followsSystem ? Kirigami.Units.cornerRadius : 4
    /// A block's tile.
    readonly property real tileRadius: metrics.followsSystem ? Kirigami.Units.cornerRadius : 5
    /// A settings panel.
    readonly property real panelRadius: metrics.followsSystem ? Kirigami.Units.cornerRadius : 6
    /// The window itself, when it draws its own frame.
    readonly property real windowRadius: metrics.followsSystem ? Kirigami.Units.cornerRadius : 8

    // **Gaps and paddings.** The prototype's own steps, which are also
    // Kirigami's at its default font: 4, 6 and 8, with 2, 10, 12, 14 and 16
    // either side of them.
    readonly property real hairSpacing: metrics.followsSystem ? Math.round(Kirigami.Units.smallSpacing / 2) : 2
    readonly property real smallSpacing: metrics.followsSystem ? Kirigami.Units.smallSpacing : 4
    readonly property real mediumSpacing: metrics.followsSystem ? Kirigami.Units.mediumSpacing : 6
    readonly property real largeSpacing: metrics.followsSystem ? Kirigami.Units.largeSpacing : 8
    /// Between an icon and its words; the side padding of a panel's row.
    readonly property real rowSpacing: metrics.followsSystem
                                       ? Kirigami.Units.largeSpacing + Math.round(Kirigami.Units.smallSpacing / 2)
                                       : 10
    /// The side padding of a panel's content and of the sidebar's panes.
    readonly property real panelPadding: metrics.followsSystem
                                         ? Kirigami.Units.largeSpacing + Kirigami.Units.smallSpacing
                                         : 12
    /// Between the groups in the header; between the sections of a pane.
    readonly property real groupSpacing: metrics.followsSystem
                                         ? Kirigami.Units.largeSpacing + Kirigami.Units.mediumSpacing
                                         : 14
    /// Between the columns of a wide layout.
    readonly property real wideSpacing: metrics.followsSystem ? Kirigami.Units.largeSpacing * 2 : 16

    // **Shadows**: how far below a settings panel its shadow falls and how
    // soft it is, resting beside its block and lifted off it.
    readonly property real shadowSize: metrics.followsSystem ? Kirigami.Units.gridUnit : 24
    readonly property real shadowOffset: metrics.followsSystem ? Kirigami.Units.smallSpacing : 8
    readonly property real liftedShadowSize: metrics.followsSystem ? Kirigami.Units.gridUnit * 2 : 40
    readonly property real liftedShadowOffset: metrics.followsSystem ? Kirigami.Units.smallSpacing * 2 : 16

    // **The prototype's layout**, in both appearances.
    /// The row across the top: tabs, status, running time, Run.
    readonly property real headerHeight: 40
    /// The row across the bottom.
    readonly property real statusBarHeight: 30
    /// The three heights of a control: a segmented choice or a small
    /// button, a panel header's button, and a field or an ordinary button.
    readonly property real smallControlHeight: 24
    readonly property real mediumControlHeight: 26
    readonly property real controlHeight: 28
    /// A library tab.
    readonly property real sidebarTabHeight: 30
    /// A settings panel's header.
    readonly property real panelHeaderHeight: 36
    /// The sidebar's width.
    readonly property real sidebarWidth: 252
    /// A block's tile, and the band across its top.
    readonly property real tileWidth: 132
    readonly property real tileHeight: 96
    readonly property real tileHeaderHeight: 24

    // **The controls' own measures**, which the Reverblabr control
    // style draws with. In *Follow system* the desktop style draws the
    // controls and none of these is read, except by a window that was
    // switched to a Reverblabr appearance and not yet restarted: so each
    // has a Kirigami value that keeps such a window in proportion.
    /// Either side of a button's label.
    readonly property real buttonPadding: metrics.followsSystem ? Kirigami.Units.largeSpacing : 12
    /// Either side of the Run button's label: the one button that is wider
    /// than its words need.
    readonly property real primaryButtonPadding: metrics.followsSystem ? Kirigami.Units.gridUnit : 18
    /// The Run button's height: a control's in the prototype, and in the
    /// desktop's look what it was before the application drew its own buttons.
    readonly property real primaryButtonHeight: metrics.followsSystem ? Math.round(Kirigami.Units.gridUnit * 1.5)
                                                                      : metrics.controlHeight
    /// Between a button's icon and its label; the Run button's glyph and
    /// word.
    readonly property real buttonSpacing: metrics.followsSystem ? Kirigami.Units.smallSpacing : 8
    /// An icon on a button or a tool button.
    readonly property real buttonIconSize: metrics.followsSystem ? Kirigami.Units.iconSizes.small : 16
    /// A check box's box and a radio button's ring.
    readonly property real indicatorSize: metrics.followsSystem ? Kirigami.Units.iconSizes.small : 15
    /// The dot inside a chosen radio button.
    readonly property real radioDotSize: metrics.followsSystem ? Math.round(Kirigami.Units.iconSizes.small / 2) : 7
    /// A switch's track, and the knob that runs along it with a gap all
    /// round.
    readonly property real switchWidth: metrics.followsSystem ? Kirigami.Units.gridUnit * 2 : 34
    readonly property real switchHeight: metrics.followsSystem ? Kirigami.Units.gridUnit : 18
    readonly property real switchKnobSize: metrics.switchHeight - metrics.switchKnobInset * 2
    readonly property real switchKnobInset: 2
    /// One choice in a segmented row, and the gap between the row's frame
    /// and the choices in it.
    readonly property real segmentHeight: metrics.followsSystem ? Kirigami.Units.gridUnit + Kirigami.Units.smallSpacing : 22
    readonly property real segmentInset: 2
    /// Either side of a list row: a library block, an application.
    readonly property real delegateHorizontalPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 6
    readonly property real delegateVerticalPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 5
    /// How far a focus ring stands out from the control it is round.
    readonly property real focusRingWidth: 2
    /// What a control that cannot be used is drawn at: the prototype's
    /// switched-off rows.
    readonly property real disabledOpacity: 0.45
    /// The halo round a selected tile, outside its border: the
    /// prototype's `0 0 0 2px`.
    readonly property real selectionHaloWidth: 2

    // Value controls: sliders, fields, lists and scroll bars.
    /// A slider's groove: the prototype's four-pixel trough, which a tile's
    /// face drew at the desktop's small spacing before there were tokens.
    readonly property real sliderGrooveHeight: metrics.followsSystem
                                               ? Math.max(2, Math.round(Kirigami.Units.smallSpacing)) : 4
    /// The ring a slider's value is dragged by: on a tile's face, and in a
    /// panel, where the prototype's slider is a sixteen-pixel row.
    readonly property real faceSliderHandleSize: metrics.followsSystem
                                                 ? Math.round(Kirigami.Units.gridUnit * 2 / 3) : 12
    readonly property real sliderHandleSize: metrics.followsSystem ? Kirigami.Units.gridUnit : 16
    /// The width of that ring.
    readonly property real sliderHandleBorder: 2
    /// Inside a field, either side of its words: a text field's, and the
    /// narrower one of a list that drops down.
    readonly property real fieldPadding: metrics.followsSystem ? Kirigami.Units.largeSpacing : 8
    readonly property real comboPadding: metrics.followsSystem ? Kirigami.Units.mediumSpacing : 6
    /// Above and below a field's words.
    readonly property real fieldVerticalPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 4
    /// A field for a web address, a name, a number: the width it asks for
    /// when nothing says otherwise, as the prototype's presets list.
    readonly property real fieldWidth: metrics.followsSystem ? Kirigami.Units.gridUnit * 8 : 140
    /// A text area's height before anything is typed into it: three lines.
    readonly property real textAreaHeight: metrics.followsSystem ? Kirigami.Units.gridUnit * 4 : 72
    /// The chevron of a list that drops down, and of a number's steps: the
    /// prototype's `M6 9l6 6 6-6` at eight pixels across.
    readonly property real chevronWidth: 8
    readonly property real chevronStroke: 1.5
    /// A number field's step buttons, stacked at its end.
    readonly property real stepperWidth: metrics.followsSystem ? Kirigami.Units.gridUnit : 16
    /// How far a list that drops down stands off its field, and the room
    /// kept between it and the window's edge.
    readonly property real popupGap: 2
    readonly property real popupMargin: 6
    /// A scroll bar: the band it takes along the edge, and the thumb in it,
    /// which widens while the pointer is over it.
    readonly property real scrollBarWidth: 8
    readonly property real scrollThumbWidth: 4
    readonly property real scrollThumbActiveWidth: 6
    /// The shortest a thumb is drawn, so a long list's is still a target.
    readonly property real scrollThumbMinimum: 24

    // Popups and messages
    //
    // **The prototype draws no menu and no tooltip** — it defines its
    // menus and never opens one, and its hints are the browser's. What it
    // does draw is one popover, the settings panel: a surface in a border,
    // six-pixel corners, a shadow, rows at a control's height. Menus,
    // tooltips and dialogs are drawn in that language, and these are the
    // few measures it does not already have a name for.
    /// Between a menu's frame and the rows in it.
    readonly property real menuPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 4
    /// The narrowest a menu is, so a menu of short words is still a menu
    /// rather than a strip.
    readonly property real menuMinimumWidth: metrics.followsSystem ? Kirigami.Units.gridUnit * 10 : 180
    /// Between a menu row's words and its key, which stands at the far end.
    readonly property real shortcutSpacing: metrics.followsSystem ? Kirigami.Units.gridUnit * 2 : 24
    /// The mark in front of a chosen row, and the arrow of a row that opens
    /// another menu: the prototype's small glyphs.
    readonly property real menuGlyphSize: metrics.followsSystem ? Kirigami.Units.iconSizes.small : 14
    /// The widest a tooltip grows before its words wrap: the help that
    /// moved off the panels into tooltips is a sentence or two.
    readonly property real toolTipMaximumWidth: metrics.followsSystem ? Kirigami.Units.gridUnit * 20 : 300

    // Geometry: where the prototype puts things, and how far apart.
    //
    // **Each one's *Follow system* value is the expression the place that
    // uses it wrote before**, so following the system keeps its own
    // spacing to the pixel and only the Reverblabr appearances move.
    /// A tile's size. The prototype's 132 by 96; about that, in the
    /// desktop's units, while following the system.
    readonly property real tileOuterWidth: metrics.followsSystem ? Math.round(Kirigami.Units.gridUnit * 7.5)
                                                                 : metrics.tileWidth
    readonly property real tileOuterHeight: metrics.followsSystem ? Math.round(Kirigami.Units.gridUnit * 5.5)
                                                                  : metrics.tileHeight
    /// A tile's header: its height, how far its contents stand in from the
    /// tile's edge, the gap between them, the block's icon and the state
    /// dot.
    readonly property real tileBandHeight: metrics.followsSystem ? Math.round(Kirigami.Units.gridUnit * 4 / 3)
                                                                 : metrics.tileHeaderHeight
    readonly property real tileHeaderInset: metrics.followsSystem ? Kirigami.Units.smallSpacing * 2 : 9
    readonly property real tileHeaderSpacing: metrics.followsSystem ? Kirigami.Units.smallSpacing : 6
    readonly property real tileIconSize: metrics.followsSystem ? Kirigami.Units.iconSizes.small : 13
    readonly property real stateDotSize: metrics.followsSystem ? Math.round(Kirigami.Units.smallSpacing * 1.5) : 7
    /// An icon face: the drawing in its middle, the gap to the line under
    /// it, and the room either side.
    readonly property real faceIconSize: metrics.followsSystem ? Kirigami.Units.iconSizes.medium : 24
    readonly property real faceSpacing: metrics.followsSystem ? Kirigami.Units.smallSpacing : 5
    readonly property real faceMargin: metrics.followsSystem ? Kirigami.Units.smallSpacing : 8
    /// A connector: the ring on a tile's edge a wire is drawn to.
    readonly property real connectorSize: metrics.followsSystem ? Math.round(Kirigami.Units.gridUnit / 2) : 9

    /// The grid of dots on the canvas: the prototype's 20 pixels, three of
    /// the eight-pixel snaps while following the system.
    readonly property real canvasGridSpacing: metrics.followsSystem ? 24 : 20
    /// The canvas's overlays, the connection mode at the top left and the
    /// zoom at the bottom right: how far in from the canvas's corner.
    readonly property real overlayInset: metrics.followsSystem ? Kirigami.Units.largeSpacing : 16
    /// The narrowest the zoom level between its two buttons is.
    readonly property real zoomLabelWidth: 34

    /// A settings panel: its width, how far it stands off its block, how
    /// close it may come to the canvas's edge, and the caret pointing at
    /// the block.
    readonly property real settingsWidth: metrics.followsSystem ? Kirigami.Units.gridUnit * 18 : 322
    readonly property real settingsGap: metrics.followsSystem ? Kirigami.Units.gridUnit : 16
    readonly property real settingsMargin: metrics.followsSystem ? Kirigami.Units.smallSpacing * 2 : 8
    readonly property real caretSize: metrics.followsSystem ? Math.round(Kirigami.Units.gridUnit * 0.6) : 10
    readonly property real caretTop: metrics.followsSystem ? Kirigami.Units.gridUnit : 23
    /// Inside a panel: the room either side of its rows, above the first
    /// and below the last, and between two rows.
    readonly property real panelSidePadding: metrics.followsSystem ? Kirigami.Units.largeSpacing : 12
    readonly property real panelEndPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 5
    readonly property real panelRowGap: metrics.followsSystem ? Kirigami.Units.smallSpacing : 10
    /// A panel's header: the room either side of it and between the things
    /// in it, and its icon.
    readonly property real panelHeaderLeftPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing * 2 : 12
    readonly property real panelHeaderRightPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 6
    readonly property real panelHeaderSpacing: metrics.followsSystem ? Kirigami.Units.smallSpacing : 8
    readonly property real panelIconSize: metrics.followsSystem ? Kirigami.Units.iconSizes.small : 14
    /// A dialog: the room round its title, its content and its
    /// buttons. A dialog is drawn as the prototype draws its one popover,
    /// the settings panel, so it takes that panel's side padding; while
    /// following the system it is Kirigami's own.
    readonly property real dialogPadding: metrics.followsSystem ? Kirigami.Units.largeSpacing : metrics.panelSidePadding
    /// A panel's footer: above and below its row.
    readonly property real panelFooterPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 8
    /// The column a panel's labels stand in, right-aligned against what
    /// they name, and the narrower one of a panel whose labels are one
    /// short word.
    readonly property real panelLabelWidth: metrics.followsSystem ? Kirigami.Units.gridUnit * 6 : 96
    readonly property real panelNarrowLabelWidth: metrics.followsSystem ? Kirigami.Units.gridUnit * 4 : 60
    /// Between a label and what it names.
    readonly property real labelSpacing: metrics.followsSystem ? Kirigami.Units.largeSpacing : 10
    /// Between two lines of one row — a slider's name and the slider under
    /// it, a choice and why it is there — which belong together more
    /// closely than two rows do. As a correction to the row gap, so while
    /// following the system nothing moves.
    readonly property real pullIntoRow: metrics.followsSystem ? 0 : 4 - metrics.panelRowGap
    /// A folding section's heading: above and below its words, and the
    /// chevron in front of them.
    readonly property real sectionTopPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 9
    readonly property real sectionBottomPadding: metrics.followsSystem ? Kirigami.Units.smallSpacing : 5
    readonly property real sectionChevronSize: metrics.followsSystem ? Kirigami.Units.iconSizes.small : 10

    /// The sidebar: how far its tabs stand in from its edges, the room
    /// round its search field, how far the library's rows stand in, and a
    /// block's icon in its box there.
    readonly property real sidebarTabsTop: metrics.followsSystem ? Kirigami.Units.smallSpacing : 8
    readonly property real sidebarTabsBottom: metrics.followsSystem ? 0 : 4
    readonly property real searchMargin: metrics.followsSystem ? Kirigami.Units.largeSpacing : 10
    readonly property real libraryInset: metrics.followsSystem ? Kirigami.Units.largeSpacing : 6
    readonly property real libraryIconSize: 15
    /// The height of a library block's two lines of words: the prototype's
    /// 12.5 pixels at 1.2, and 10.5 at 1.25.
    readonly property real libraryTitleLine: 15
    readonly property real libraryDetailLine: 13

    /// How fast a live wire's dashes run, in the canvas's phase, which goes
    /// round in ten: the prototype's 24 pixels in 0.9 seconds, a round of
    /// its twelve-unit dash.
    readonly property real flowSpeed: 10 / 0.9

    // Title bar: the frameless window of the Reverblabr appearances,
    // whose top row is the prototype's header.
    /// A button in the header — the theme, the menu and the three window
    /// buttons — is a control's height square.
    readonly property real windowButtonSize: metrics.controlHeight
    /// The window buttons' glyphs, which the prototype draws at 22 in 28.
    readonly property real windowGlyphSize: 22
    /// The rule between the header's groups: 20 pixels of the border colour.
    readonly property real headerDividerHeight: 20
    /// How far in from the window's edge a press resizes it, and the square
    /// in each corner that resizes both ways. The prototype has none; these
    /// are the width of the compositor's own borderless grip.
    readonly property real resizeGrip: 6
    readonly property real resizeCorner: 16
}

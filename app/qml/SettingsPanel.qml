// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kirigami.primitives as Primitives
import org.kde.ki18n

// One block's settings, opened beside that block.
//
// **A block's settings belong to that block**, so they open next to it
// rather than in a pane at the side of the window. The canvas places it —
// it is the only thing that knows where a tile has got to — and this file
// is what the panel *is*: a card, a header, the block's own body and the
// shared presets footer.
//
// **It is not inside the zoom transform.** Scaled with the tiles its text
// would blur at 200 % and be unreadable at 50 %, and its controls would be
// the wrong size to hit. The canvas works out where the block has got to in
// viewport pixels and puts the panel there at its own size.
//
// **One panel, whichever block is showing.** It is opened by naming a block
// rather than by being created for one, so the canvas has nothing to own and
// nothing to tidy up, and moving from one tile to the next is a change of
// one string.
//
// The body is the block's own panel where there is one, and the generic
// panel — generated from the parameter descriptors — everywhere else. The
// generic one is not a lesser panel: it is right for anything whose settings
// are a list of settings, and it is the accessible fallback for the rest.
Primitives.ShadowedRectangle {
    id: panel

    /// The block being shown. Empty closes the panel.
    property string blockId: ""

    /// Which edge the caret points out of: "left" when the panel sits to
    /// the right of its block, "right" when it has flipped to the left of
    /// it, empty for none. **The canvas decides**, because whether there
    /// was room is its question and not the panel's.
    property string caret: ""

    /// The tallest the panel may be. The canvas sets it from what is left
    /// of the viewport; the panel is as tall as its contents up to this,
    /// and scrolls inside it after that.
    property real maximumHeight: Kirigami.Units.gridUnit * 30

    /// The widest the panel may be. The canvas sets it from the
    /// viewport's width, so that in a narrow window the card is narrowed
    /// with it rather than left half outside; the panel is as wide as its
    /// body asks up to this.
    property real maximumWidth: Infinity

    /// Whether the panel has been pulled off its block.
    ///
    /// **A detached panel is a thing in its own right.** It stays where it
    /// is put, it does not follow the block about, and it says so three
    /// ways: the caret goes, the border takes the accent colour and the
    /// shadow deepens. One signal would do for somebody looking straight at
    /// it; three are for somebody who is not.
    ///
    /// **The canvas holds the state, not the panel**, because where a
    /// detached panel sits is the canvas's arithmetic exactly as the docked
    /// one is.
    property bool detached: false

    /// The detach control was pressed. The canvas decides what that means
    /// and where the panel ends up.
    signal detachToggled()
    /// The header was dragged, in the panel's own parent's coordinates.
    signal draggedBy(real dx, real dy)
    /// A drag of the header ended, which is when where it was left is worth
    /// writing down.
    signal dragFinished()
    /// Anywhere on the card was pressed. **Several detached panels can
    /// overlap**, and the one somebody reaches for comes to the top.
    signal pressedOn()

    /// The presets row at the foot of the panel.
    readonly property alias presets: presetsSlot
    /// What is showing: the block's own panel, or the generic one. **For a
    /// test**, and for anything that has to reach a panel it did not write.
    readonly property alias body: body
    /// What the body scrolls in, for a test that asks how far it goes.
    readonly property alias bodyView: bodyView
    /// The parts of the header, for a test that presses what a person
    /// presses.
    readonly property alias closeButton: closeButton
    readonly property alias detachButton: detachButton
    /// The header's own pointer area: a double-click on the name renames,
    /// and once the panel is detached a drag moves it.
    readonly property alias headerGrab: headerGrab
    readonly property alias enabledSwitch: enabledSwitch
    readonly property alias nameField: nameField
    readonly property alias nameLabel: nameLabel

    readonly property var blockInfo: SessionModel.revision >= 0 && panel.blockId !== ""
        ? SessionModel.block(panel.blockId)
        : ({})
    readonly property string typeId: panel.blockInfo.type ?? ""
    /// What the block is called: what somebody typed, or its type's own
    /// name.
    ///
    /// **The same lookup the tile uses**. This used to fall back to
    /// the type id, under a note saying it would until the catalogue
    /// content task wrote the presentation table — which it did, and the
    /// tile has read it ever since, so a panel opened on an unnamed block
    /// was headed `effect.dynamics` while its tile said "Dynamics
    /// Processor". A block whose type this build has never heard of still
    /// gets its id, because that is what the table hands back and it is
    /// better than an empty header.
    readonly property string displayName: (panel.blockInfo.nickname ?? "") !== ""
        ? panel.blockInfo.nickname
        : BlockPresentation.name(panel.typeId)

    visible: panel.blockId !== ""
    color: AppColors.surface
    radius: AppMetrics.panelRadius
    border.width: 1
    border.color: panel.detached ? AppColors.accentMark : AppColors.border

    // **The shadow is how far off the canvas it reads.** Docked, it is a
    // card resting on the canvas beside its block; detached, it is lifted
    // clear of it and belongs to nothing underneath.
    shadow.color: panel.detached ? AppColors.liftedShadow : AppColors.shadow
    shadow.size: panel.detached ? AppMetrics.liftedShadowSize : AppMetrics.shadowSize
    shadow.yOffset: panel.detached ? AppMetrics.liftedShadowOffset : AppMetrics.shadowOffset

    /// What the block's own panel asks of the card, or \a otherwise when
    /// it asks nothing: its width, and whether it wants the presets row.
    /// **Asked of whatever panel is there**, because which panel
    /// that is is the register's business and most of them ask nothing.
    function bodyAsks(name: string, otherwise: var): var {
        const item = body.item;
        return item !== null && item[name] !== undefined ? item[name] : otherwise;
    }

    /// A block whose settings are one tall meter wants a narrow card, and
    /// one with a list of settings the standard one. **Every card is a
    /// button wider than its body asks**, for the help button in the footer:
    /// the footer is the card's and not the body's, so the room for it is
    /// added here rather than asked of every panel.
    implicitWidth: Math.min(panel.maximumWidth,
                            panel.bodyAsks("panelWidth", AppMetrics.settingsWidth) + AppMetrics.settingsButtonRoom)
    // **As tall as what is in it**, up to what the canvas allows. A block
    // with three settings should not get a panel the height of the window;
    // the body is the only part that can grow without bound, so the cap
    // falls on the whole and the body scrolls inside it.
    //
    // **Asked of the layout, not added up here.** A sum written out by hand
    // is a sum that forgets a margin — it forgot two, and the last row of
    // every panel was cut in half.
    implicitHeight: Math.min(panel.maximumHeight, contents.implicitHeight + panel.border.width * 2)

    signal closed()

    function open(id: string) {
        panel.blockId = id;
    }

    function close() {
        panel.blockId = "";
        panel.closed();
    }

    /// Starts renaming, which a double-click on the name does. **F2 is the
    /// window's**, and renames the block that is selected: with
    /// several panels open at once, a panel's own F2 could not say which of
    /// them it meant.
    function startRenaming() {
        nameField.text = panel.blockInfo.nickname ?? "";
        nameField.visible = true;
        nameField.forceActiveFocus();
        nameField.selectAll();
    }

    // **A press on the card stays on the card**. The card is a
    // rectangle, which takes no press of its own, so anywhere no control
    // took one — the header of a docked panel, the gap between two rows —
    // it fell through to the canvas underneath, and a press on the canvas
    // closes the settings. First, so every control is above it.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: panel.pressedOn()
    }

    ColumnLayout {
        id: contents

        anchors.fill: parent
        // Inside the border rather than over it, or the card would have no
        // outline anywhere the header or the footer reaches.
        anchors.margins: panel.border.width
        spacing: 0

        // --- header: which block, its name, on/off, and close ---
        //
        // **Close is on the right**, where a window's close is and where
        // the design puts it. It used to be on the left, which put the one
        // destructive control in the header first in the reading order.
        Rectangle {
            id: header

            Layout.fillWidth: true
            // The prototype's 36 pixels and the rule under them; the row's
            // own height and a margin while following the system.
            implicitHeight: AppMetrics.followsSystem ? headerRow.implicitHeight + Kirigami.Units.smallSpacing * 2
                                                     : AppMetrics.panelHeaderHeight + 1
            color: AppColors.surface2
            radius: panel.radius

            // The header is the top of the card, so its top corners are the
            // card's and its bottom ones are square where it meets the body.
            Rectangle {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                height: parent.radius
                color: parent.color
            }

            Kirigami.Separator {
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            }

            // **The header is what a detached panel is dragged by**, the way
            // a window is dragged by its title bar, and **a double-click on
            // the name renames the block**, docked or detached — the
            // name is the thing being changed, so it is what is pressed,
            // rather than a pencil beside it.
            //
            // **Declared before the controls**, so the buttons and the name
            // field are above it and take their own presses; what reaches
            // here is a press on the header itself. The name is a label,
            // which takes none, so a press on it arrives here too.
            MouseArea {
                id: headerGrab

                anchors.fill: parent
                cursorShape: panel.detached ? Qt.OpenHandCursor : Qt.ArrowCursor
                acceptedButtons: Qt.LeftButton

                property point origin: Qt.point(0, 0)

                onPressed: mouse => {
                    headerGrab.origin = Qt.point(mouse.x, mouse.y);
                    panel.pressedOn();
                    if (panel.detached) {
                        headerGrab.cursorShape = Qt.ClosedHandCursor;
                    }
                }
                onReleased: {
                    headerGrab.cursorShape = panel.detached ? Qt.OpenHandCursor : Qt.ArrowCursor;
                    if (panel.detached) {
                        panel.dragFinished();
                    }
                }
                onCanceled: headerGrab.cursorShape = panel.detached ? Qt.OpenHandCursor : Qt.ArrowCursor
                onDoubleClicked: mouse => {
                    const onName = nameLabel.mapFromItem(headerGrab, mouse.x, mouse.y);
                    if (nameLabel.visible && nameLabel.contains(onName)) {
                        panel.startRenaming();
                    }
                }
                // **How far it has come since the press**, not where the
                // pointer is: the panel moves under the pointer, so a
                // position measured against the header itself would be a
                // position that never changes.
                onPositionChanged: mouse => {
                    if (!panel.detached || (mouse.buttons & Qt.LeftButton) === 0) {
                        return;
                    }
                    panel.draggedBy(mouse.x - headerGrab.origin.x, mouse.y - headerGrab.origin.y);
                }
            }

            RowLayout {
                id: headerRow

                anchors.fill: parent
                anchors.leftMargin: AppMetrics.panelHeaderLeftPadding
                anchors.rightMargin: AppMetrics.panelHeaderRightPadding
                anchors.topMargin: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : 0
                anchors.bottomMargin: AppMetrics.followsSystem ? Kirigami.Units.smallSpacing : 1
                spacing: AppMetrics.panelHeaderSpacing

                // **The same icon and the same tint the tile carries**, so
                // that a panel and the block it belongs to are obviously
                // the same thing.
                Kirigami.Icon {
                    source: BlockPresentation.icon(panel.typeId)
                    color: AppColors.categoryTint(BlockPresentation.category(panel.typeId))
                    isMask: true
                    implicitWidth: AppMetrics.panelIconSize
                    implicitHeight: AppMetrics.panelIconSize
                    Accessible.ignored: true
                }

                ElidedLabel {
                    id: nameLabel

                    visible: !nameField.visible
                    text: panel.displayName
                    color: AppColors.text
                    // Bold while following the system; the prototype's
                    // semi-bold, where its name field keeps its words in
                    // from the field's edge.
                    font.weight: AppMetrics.followsSystem ? AppFonts.bold : AppFonts.demiBold
                    leftPadding: AppMetrics.followsSystem ? 0 : AppMetrics.smallSpacing
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                QQC2.TextField {
                    id: nameField

                    visible: false
                    Layout.fillWidth: true
                    Accessible.name: KI18n.i18nc("@label:textbox", "Block name")
                    // The block's own name, so that clearing the field shows
                    // what clearing it will leave behind.
                    placeholderText: BlockPresentation.name(panel.typeId)
                    onEditingFinished: {
                        SessionModel.renameBlock(panel.blockId, text);
                        visible = false;
                    }
                    Keys.onEscapePressed: visible = false
                }

                QQC2.Switch {
                    id: enabledSwitch

                    Accessible.name: KI18n.i18nc("@action:button switch a block on or off", "Block enabled")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onToggled: SessionModel.setBlockEnabled(panel.blockId, enabledSwitch.checked)

                    SessionBound {
                        control: enabledSwitch
                        property: "checked"
                        value: panel.blockInfo.enabled !== false
                    }
                }

                // **Between the switch and close**, as the design has it:
                // the two controls that change what the panel *is* sit
                // together, and close stays last.
                QQC2.ToolButton {
                    id: detachButton

                    icon.name: panel.detached ? "window-pin" : "window-new"
                    display: QQC2.AbstractButton.IconOnly
                    Accessible.name: panel.detached
                        ? KI18n.i18nc("@action:button put the settings back beside their block", "Attach to block")
                        : KI18n.i18nc("@action:button pull the settings off into a window of their own",
                                      "Detach as window")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: panel.detachToggled()

                    // The prototype's 26-pixel square; the desktop style's
                    // own size while following the system.
                    Binding on implicitWidth {
                        when: !AppMetrics.followsSystem
                        value: AppMetrics.mediumControlHeight
                    }
                    Binding on implicitHeight {
                        when: !AppMetrics.followsSystem
                        value: AppMetrics.mediumControlHeight
                    }
                }

                QQC2.ToolButton {
                    id: closeButton

                    icon.name: "dialog-close"
                    display: QQC2.AbstractButton.IconOnly
                    Accessible.name: KI18n.i18nc("@action:button", "Close the settings panel")
                    QQC2.ToolTip.text: Accessible.name
                    QQC2.ToolTip.visible: hovered
                    QQC2.ToolTip.delay: Kirigami.Units.toolTipDelay
                    onClicked: panel.close()

                    // The prototype's 26-pixel square; the desktop style's
                    // own size while following the system.
                    Binding on implicitWidth {
                        when: !AppMetrics.followsSystem
                        value: AppMetrics.mediumControlHeight
                    }
                    Binding on implicitHeight {
                        when: !AppMetrics.followsSystem
                        value: AppMetrics.mediumControlHeight
                    }
                }
            }
        }

        // --- body: the block's own panel, or the generic one ---
        //
        // **The height is asked of the panel inside, not of the view.** A
        // ScrollView's own implicitHeight is zero, so a panel sized from the
        // layout and a layout sized from the panel would be two things each
        // waiting for the other. QML settles such a pair on *some* answer —
        // it settled on one a few pixels short, which cut the last row off
        // every panel — rather than failing, which is what makes it worth
        // saying out loud here.
        QQC2.ScrollView {
            id: bodyView

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: body.implicitHeight
            Layout.minimumHeight: 0
            // **Room at the sides**, as every panel in the design has it: a
            // label or a slider's end right against the card's edge reads
            // as cut off.
            Layout.topMargin: AppMetrics.panelEndPadding
            Layout.bottomMargin: AppMetrics.panelEndPadding
            // **In the Reverblabr appearance the view is the card's full
            // width** and the rows are inset inside it: a section's
            // rule runs edge to edge of the card, as the prototype draws
            // it, which it could not do inside a view that clipped it at
            // the rows' edge.
            Layout.leftMargin: AppMetrics.followsSystem ? AppMetrics.panelSidePadding : 0
            Layout.rightMargin: AppMetrics.followsSystem ? AppMetrics.panelSidePadding : 0
            // **The card says how wide; the view does not.** Its implicit
            // width is its content's, which is its own available width, so
            // left to itself it would go on asking for whatever it was last
            // given — and it once was given more than the card had.
            Layout.minimumWidth: 0
            Layout.preferredWidth: 0
            contentWidth: availableWidth
            // **And the panel inside says how tall**, every time: left to
            // work it out, the view kept the tallest panel it had shown, so
            // a Peak/RMS panel's height went on being scrolled through
            // under an Output Device's, into nothing (unnoticed until the
            // scroll bar, shown whenever there is more, began to say so).
            contentHeight: body.implicitHeight

            Loader {
                id: body

                readonly property real inset: AppMetrics.followsSystem ? 0 : AppMetrics.panelSidePadding

                x: body.inset
                width: parent.width - body.inset * 2
                // **A block with no panel of its own is not a lesser
                // block.** The generic panel is generated from what it says
                // about itself, which for most blocks is the right panel.
                sourceComponent: BlockPanels.panelFor(panel.typeId) ?? generic
            }

            // **A block's own panel is told which block it is.** The
            // component comes from the registry and is created in the
            // registry's context, so the loader's own properties are not
            // something it can see; the one thing it needs is handed to it.
            Binding {
                target: body.item
                property: "blockId"
                value: panel.blockId
                when: body.item !== null
            }
        }

        Component {
            id: generic

            GenericParamPanel {
                blockId: panel.blockId
            }
        }

        Kirigami.Separator {
            id: footerRule

            visible: presetsSlot.visible
            Layout.fillWidth: true
        }

        // --- footer: presets ---
        //
        // **One footer for every block**, here rather than in each panel:
        // a block that can be set from a preset is set from one the same
        // way, whichever panel it has.
        PresetsFooter {
            id: presetsSlot

            blockId: panel.blockId
            // Nothing to show until there is a block. **A block whose
            // settings say they have no use for presets** (a meter, whose
            // only settings are how it looks) keeps the row without them:
            // its settings can still be put back to their defaults.
            visible: panel.blockId !== ""
            showsPresets: panel.bodyAsks("wantsPresets", true)
            Layout.fillWidth: true
            Layout.topMargin: AppMetrics.panelFooterPadding
            Layout.bottomMargin: AppMetrics.panelFooterPadding
            Layout.leftMargin: AppMetrics.panelSidePadding
            Layout.rightMargin: AppMetrics.panelSidePadding
        }
    }

    // **The caret, pointing back at the block the panel belongs to.** A
    // square turned on its corner, half of it outside the card, drawn in the
    // header's colour so that it reads as a corner of the card rather than
    // as a diamond stuck to it.
    //
    // **Last, so that it paints over the card's own border**, which is what
    // makes the outline bulge into the caret instead of running straight
    // across its base. The square inside it covers the two sides that face
    // the card, leaving a border only on the two that face the block.
    //
    // It sits level with the header rather than at the panel's middle: a
    // panel is as tall as its contents, and the middle of a tall one points
    // at nothing near the block.
    Rectangle {
        id: caretMark

        visible: panel.caret !== "" && !panel.detached
        width: AppMetrics.caretSize
        height: width
        x: panel.caret === "right" ? panel.width - width / 2 : -width / 2
        y: AppMetrics.caretTop
        rotation: 45
        antialiasing: true
        color: AppColors.surface2
        border.width: 1
        border.color: AppColors.border
        Accessible.ignored: true

        Rectangle {
            anchors.fill: parent
            // A whole square's worth, shifted a pixel off the two sides
            // that face the card, so those two borders are painted over and
            // the other two are left showing.
            anchors.leftMargin: panel.caret === "right" ? -1 : 1
            anchors.rightMargin: panel.caret === "right" ? 1 : -1
            anchors.topMargin: panel.caret === "right" ? 1 : -1
            anchors.bottomMargin: panel.caret === "right" ? -1 : 1
            antialiasing: true
            color: parent.color
        }
    }

}

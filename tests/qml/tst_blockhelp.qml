// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A block's help, offscreen against the mock engine: that the button in a
// settings card opens the help window, a window of its own above the
// others, that one window serves every block, and that every help page
// names every control its block's panel shows.

import QtQuick
import QtQuick.Templates as T
import QtTest
import org.kde.kirigami as Kirigami
import io.github.reverblabr

TestCase {
    id: testCase

    name: "BlockHelp"
    when: windowShown

    property string sessionId: ""

    Component {
        id: shownPanel

        Window {
            property alias panel: inner

            width: 480
            height: 640
            visible: true

            SettingsPanel {
                id: inner

                width: 400
                height: 600
            }
        }
    }

    Component {
        id: genericComponent

        GenericParamPanel {
            width: 360
        }
    }

    function initTestCase() {
        TestEngine.sessionOpened.connect(function (id) {
            testCase.sessionId = id;
        });
        TestEngine.createSession("Help");
        verify(testCase.sessionId !== "", "the mock opened a session");
        SessionModel.sessionId = testCase.sessionId;
        tryVerify(() => BlockCatalogue.ready, 2000, "the engine answered with a catalogue");
    }

    function cleanup() {
        if (BlockHelp.window !== null) {
            BlockHelp.window.close();
        }
    }

    function addBlock(id: string, typeId: string) {
        if (SessionModel.blockType(id) === "") {
            TestEngine.applyEdit(testCase.sessionId,
                                 JSON.stringify({ op: "addBlock", block: { id: id, type: typeId }, index: -1 }));
        }
        tryVerify(() => SessionModel.blockType(id) === typeId, 1000, "the mock did not add a " + typeId);
    }

    // --- the button and the window ---

    function test_theButtonInTheFooterOpensTheBlocksHelp() {
        addBlock("vol", "effect.volume");
        const window = createTemporaryObject(shownPanel, null);
        const panel = window.panel;
        panel.open("vol");
        waitForRendering(panel);
        const button = panel.presets.helpButton;
        verify(button.visible, "the footer has no help button");
        verify(button.icon.name === "help-contextual" || String(button.icon.source).endsWith("glyph.help.svg"),
               "the help button shows neither the theme's help nor our circled question mark");
        // **Beside the menu**, as the last but one thing on the row.
        verify(button.x < panel.presets.menuButton.x, "the help button is not before the menu");

        mouseClick(button);
        tryVerify(() => BlockHelp.window !== null && BlockHelp.window.visible, 2000, "the help window did not open");
        const help = BlockHelp.window;
        compare(help.typeId, "effect.volume");
        compare(help.heading.text, BlockPresentation.name("effect.volume"));
        verify(help.title.indexOf(BlockPresentation.name("effect.volume")) >= 0, "the window's title does not name the block");
        // **A window of its own**, not a dialog in the card's window, and
        // above the others.
        verify(help !== window, "the help is drawn in the card's own window");
        compare(help.transientParent, null);
        verify((help.flags & Qt.WindowStaysOnTopHint) !== 0, "the help window is not kept above the others");
        verify(help.page !== null, "the Volume block has no help page");
        verify(help.sectionList.count >= 5, "the page has too few parts to be a page");
    }

    function test_oneWindowServesEveryBlock() {
        const first = BlockHelp.show("effect.volume", null);
        tryVerify(() => first.visible, 2000);
        const second = BlockHelp.show("meter.peakrms", null);
        compare(second, first, "a second block's help opened a second window");
        compare(second.typeId, "meter.peakrms");
        compare(second.heading.text, BlockPresentation.name("meter.peakrms"));
    }

    function test_aBlockWithNoPageYetStillGetsTheWindow() {
        // **Any block of the catalogue without a page**, rather than one
        // named here that a page written later would take away: a plugin
        // block never has one (tests/tools/help-pending.txt leaves them out),
        // so there is always one to ask about.
        const typeId = BlockCatalogue.types.find(type => BlockHelp.pageFor(type) === null) ?? "";
        verify(typeId !== "", "every block of the catalogue has a page, and none is left to show the placeholder");
        const help = BlockHelp.show(typeId, null);
        tryVerify(() => help.visible, 2000);
        compare(help.page, null);
        compare(help.summaryLabel.text, BlockPresentation.description(typeId));
        compare(help.sectionList.count, 1);
    }

    function test_theTextKeepsToTheMeasure() {
        const help = BlockHelp.show("output.recorder", null);
        tryVerify(() => help.visible, 2000);
        help.width = 1400;
        waitForRendering(help.contentItem);
        // **A wide window adds margin, not longer lines.**
        verify(help.column.width <= help.measureWidth + 1, "the column is wider than the measure");
        verify(help.column.x > AppMetrics.helpPagePadding, "the column is not centred in a wide window");
        verify(help.measureWidth > 0);
        help.width = 360;
        waitForRendering(help.contentItem);
        verify(help.column.width <= 360 - AppMetrics.helpPagePadding * 2 + 1, "the column runs past a narrow window");
    }

    function test_bothAppearancesDrawThePageWithTheirOwnColours() {
        const help = BlockHelp.show("effect.volume", null);
        tryVerify(() => help.visible, 2000);
        const before = Appearance.mode;
        Appearance.mode = Appearance.ReverblabrDark;
        const dark = String(help.heading.color);
        compare(help.heading.color, AppColors.text);
        Appearance.mode = Appearance.ReverblabrLight;
        compare(help.heading.color, AppColors.text);
        verify(String(help.heading.color) !== dark, "the heading is the same colour in both appearances");
        Appearance.mode = before;
    }

    // --- every page against its panel ---

    function test_everyPageNamesEveryControlOfItsPanel_data() {
        return Object.keys(BlockHelp.pages).map(typeId => ({ tag: typeId, typeId: typeId }));
    }

    /// The names a page gives: every heading, and what each section says
    /// it also covers.
    function namesOf(page) {
        const names = [];
        for (const section of page.sections) {
            names.push(section.title);
            for (const name of section.covers) {
                names.push(name);
            }
        }
        return names;
    }

    /// Every control in \a item a person can reach, by what it is called:
    /// a screen reader's name, or its words. **Not inside a control**, whose
    /// parts are its own, and **not one the keyboard never reaches**, which
    /// is a shortcut for what another control does (the file-name pieces
    /// type into the name's box). One inside a row a name of the page
    /// covers is covered by it: the choices of Overdrive are Overdrive.
    function controlsOf(item, names, covered, found) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i];
            // **Not a message with nothing to press.** Kirigami's
            // InlineMessage always holds a close button, hidden unless it
            // is asked for; a warning that offers neither it nor an action
            // has no control in it, whatever its parts are.
            if (child instanceof Kirigami.InlineMessage && !child.showCloseButton && child.actions.length === 0) {
                continue;
            }
            const own = child.Accessible.name || "";
            const coveredHere = covered || (own !== "" && names.indexOf(own) >= 0);
            // **The templates' types**: a style's control is a type of its
            // own derived from them, which instanceof does not see as the
            // Controls module's.
            const isControl = child instanceof T.AbstractButton || child instanceof T.Slider
                || child instanceof T.SpinBox || child instanceof T.ComboBox
                || child instanceof T.TextField || child instanceof T.TextArea
                || child instanceof T.RangeSlider || child instanceof T.Dial;
            if (isControl) {
                const shortcut = child instanceof T.AbstractButton && child.focusPolicy === Qt.NoFocus;
                if (!shortcut && !child.Accessible.ignored) {
                    found.push({ name: own || (child.text ?? ""), covered: coveredHere });
                }
                continue;
            }
            controlsOf(child, names, coveredHere, found);
        }
        return found;
    }

    function test_everyPageNamesEveryControlOfItsPanel(data) {
        // **A block this build leaves out** (Broadcast without QtKeychain,
        // Speech Denoise without RNNoise) has a page but no panel to hold
        // it against.
        if (Object.keys(BlockCatalogue.describe(data.typeId)).length === 0) {
            skip(data.typeId + " is not in this build");
        }
        const component = BlockHelp.pageFor(data.typeId);
        verify(component !== null);
        const page = createTemporaryObject(component, null);
        verify(page !== null, "the page for " + data.typeId + " was not made");
        compare(page.typeId, data.typeId, "the page is filed under another block");
        verify(page.summary.length > 0, "the page says nothing under its title");
        verify(page.sections.length > 0, "the page has no parts");
        const settings = Array.from(page.sections).filter(section => section.setting);
        verify(settings.length > 0, "the page explains no setting");
        // **The parts every page has**, by the shared headings.
        for (const part of [BlockHelp.aboutTitle, BlockHelp.whenTitle, BlockHelp.tipsTitle]) {
            verify(Array.from(page.sections).some(section => section.title === part && !section.setting),
                   data.typeId + "'s page has no part called " + part);
        }
        for (const section of page.sections) {
            verify(section.title.length > 0, "a section of " + data.typeId + "'s page has no heading");
        }

        const id = "help-" + data.typeId.replace(/\./g, "-");
        addBlock(id, data.typeId);
        const panelComponent = BlockPanels.panelFor(data.typeId) ?? genericComponent;
        const panel = createTemporaryObject(panelComponent, null, { blockId: id, width: 360 });
        verify(panel !== null, "no panel for " + data.typeId);
        wait(50);

        const names = namesOf(page);
        const controls = controlsOf(panel, names, false, []);
        // **A block with nothing to set** (Join L/R) has a panel that says
        // so, and nothing in it to name.
        if (BlockCatalogue.params(data.typeId).length === 0) {
            compare(controls.length, 0, data.typeId + " has no settings and yet its panel has controls");
            return;
        }
        verify(controls.length > 0, "found no controls in " + data.typeId + "'s panel");
        const missing = controls.filter(control => !control.covered && names.indexOf(control.name) < 0)
            .map(control => "\"" + control.name + "\"");
        compare(missing.join(", "), "", data.typeId + "'s help names no section after these controls of its panel");
    }
}

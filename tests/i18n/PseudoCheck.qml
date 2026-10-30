// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Drives pseudocheck.cpp: opens the key screens in the pseudo-locale
// one after another and looks at every piece of text drawn on each.
//
// - **Untranslated:** visible text with letters in it and no ⟦ — it never
//   went through the catalog. Data the mock engine made up (device and
//   application names, the session's own name) is listed in `data` below.
// - **Cut off:** a Text that says it is `truncated` (elided, or more lines
//   than it may show), or one that neither elides nor wraps and is wider
//   than the room it was given.
//
// The screens: the window with every block on the canvas, stopped and
// running; the block library, Files and Info; every block's settings panel;
// the main menu and each of its menus; the session dialogs; the Virtual
// Devices window; the Sessions window; the tray's popover; the Recording
// Inspector; the blocks' help.

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.ki18n
import io.github.reverblabr

QtObject {
    id: driver

    /// Words that are data, not the application's: what the mock engine
    /// calls its devices and applications, and what this check calls its
    /// session and recording. A real person's are just as untranslatable.
    readonly property var data: [
        "Pseudo check", "Pseudo take", "Firefox", "Music Player", "USB Microphone", "HDMI", "Headphones",
        "Every app", "Built-in Audio", "Speakers", "Microphone",
        // The mock's plugin: its name, id and parameter are the plugin's words.
        "Test gain", "test.gain", "gain"
    ]

    /// Words Qt draws from its own catalog, which KI18n loads for a real
    /// language (the pseudo-locale has no Qt part): a dialog's standard
    /// buttons, and a shortcut written out by QKeySequence.
    readonly property var qtButtons: ["OK", "Cancel", "Close", "Apply", "Yes", "No", "Save", "Discard", "Reset", "Help",
                                      "Open", "Abort", "Retry", "Ignore"]
    readonly property var shortcut: /^((Ctrl|Alt|Shift|Meta)\+)*([^\s]{1,3}|F\d{1,2}|Space|Tab|Esc|Del|Delete|Backspace|Return|Enter|Home|End|PgUp|PgDown|Ins|Left|Right|Up|Down)$/

    property var window: null
    property var ids: []
    property var findings: []
    property var seen: ({})
    property int screens: 0
    property int elidedWithToolTip: 0
    property var steps: []
    property int stepIndex: 0
    property double stepStarted: 0
    property string recordingPath: ""

    readonly property Component mainComponent: Component {
        Main {}
    }

    readonly property Timer ticker: Timer {
        interval: 50
        repeat: true
        onTriggered: driver.advance()
    }

    function fail(message: string) {
        console.error("pseudo check: " + message);
        Checker.finish(2);
    }

    function step(label: string, action: var, ready: var, settleMs: int) {
        steps.push({ label: label, action: action, ready: ready || (() => true), settle: settleMs || 0, done: false });
    }

    function advance() {
        if (stepIndex >= steps.length) {
            ticker.stop();
            finishUp();
            return;
        }
        const current = steps[stepIndex];
        const now = Date.now();
        if (!current.done) {
            current.done = true;
            stepStarted = now;
            current.action();
            return;
        }
        if (now - stepStarted < current.settle || !current.ready()) {
            if (now - stepStarted > 10000) {
                fail("timed out at: " + current.label);
            }
            return;
        }
        ++stepIndex;
    }

    // --- Looking at what is drawn ---

    /// A short path to an item, for the report: its type and its parents'.
    function pathOf(item: var): string {
        const parts = [];
        for (let at = item; at !== null && parts.length < 6; at = at.parent) {
            const name = at.objectName ? at.objectName : String(at).replace(/\(0x[0-9a-f]+.*$/, "").replace(/_QMLTYPE_\d+/, "");
            parts.unshift(name);
        }
        return parts.join(" > ");
    }

    function isData(text: string, item: var): bool {
        if (data.some(word => text === word || text.startsWith(word + " ") || text.endsWith(" " + word))) {
            return true;
        }
        if (shortcut.test(text) || (qtButtons.indexOf(text) >= 0 && pathOf(item).indexOf("DialogButtonBox") >= 0)) {
            return true;
        }
        // A folder, as a field's placeholder shows the default one; and the
        // Recorder's file-name tokens, which are the same in every language.
        return text.startsWith("/") || text.startsWith("~") || /^(%\w+[\s._\-/]*)+$/.test(text);
    }

    function report(screen: string, kind: string, item: var, text: string, detail: string) {
        const key = kind + "|" + text;
        if (seen[key] !== undefined) {
            return;
        }
        seen[key] = true;
        const at = item.mapToItem(null, 0, 0);
        const finding = { screen: screen, kind: kind, text: text, detail: detail, path: pathOf(item),
                          rect: [Math.round(at.x), Math.round(at.y), Math.round(item.width), Math.round(item.height)] };
        findings.push(finding);
        console.log("FINDING " + kind + " on " + screen + ": \"" + text + "\" " + detail + " at " + finding.path);
    }

    function look(screen: string, item: var) {
        if (item === null || item === undefined || !item.visible || item.opacity === 0) {
            return;
        }
        if (item instanceof Text) {
            lookAtText(screen, item);
        } else if (item instanceof TextInput || item instanceof TextEdit) {
            // What a person typed is theirs; a field's placeholder is ours.
            const field = item.parent;
            if (field && field.placeholderText !== undefined && field.placeholderText !== "" && item.text === "") {
                lookAtWords(screen, field, field.placeholderText);
            }
        }
        for (const child of item.children) {
            look(screen, child);
        }
    }

    function lookAtWords(screen: string, item: var, text: string) {
        if (text.indexOf("⟦") < 0 && /[A-Za-z]{2,}/.test(text) && !isData(text, item)) {
            report(screen, "untranslated", item, text, "");
        }
    }

    function lookAtText(screen: string, text: var) {
        const words = String(text.text);
        if (words === "" || text.width <= 0) {
            return;
        }
        lookAtWords(screen, text, words);
        let detail = "";
        if (text.truncated) {
            detail = "(elided, " + Math.round(text.width) + " px wide)";
        } else if (text.elide === Text.ElideNone && text.wrapMode === Text.NoWrap
                   && text.contentWidth > text.width + 1) {
            detail = "(needs " + Math.round(text.contentWidth) + " px, has " + Math.round(text.width) + ")";
        }
        if (detail !== "" && !readableInFull(text, words)) {
            report(screen, "cut off", text, words, detail);
        } else if (detail !== "") {
            ++elidedWithToolTip;
        }
    }

    /// Whether text that doesn't fit can still be read in full: a ToolTip
    /// on it, or on one of the few items around it, says the same words or
    /// says more (the library's one-line description has its longer form in
    /// the row's tooltip). That is eliding by design; without it, the words
    /// are simply lost.
    function readableInFull(text: var, words: string): bool {
        let at = text;
        for (let level = 0; at !== null && level < 6; ++level, at = at.parent) {
            const tip = Checker.toolTipOf(at);
            if (tip !== "" && (tip.indexOf(words) >= 0 || tip.length >= words.length)) {
                return true;
            }
        }
        return false;
    }

    /// A window's root item: its content with the header, menu bar and
    /// the overlay popups are drawn in, which `contentItem` leaves out.
    function rootOf(window: var): var {
        return window.contentItem.parent ?? window.contentItem;
    }

    function screen(name: string, shotOf: var, roots: var, settleMs: int) {
        step(name, () => {}, null, settleMs || 250);
        step(name + ": look", () => {
            ++screens;
            for (const root of roots()) {
                look(name, root);
            }
            const shot = shotOf();
            if (shot && !Checker.grab(shot, name.replace(/[^A-Za-z0-9.-]+/g, "-"))) {
                fail("could not grab " + name);
            }
        });
    }

    // --- The scene ---

    function buildScene() {
        SessionModel.renameSession("Pseudo check");
        const types = BlockCatalogue.types;
        const made = [];
        let index = 0;
        for (const typeId of types) {
            const id = SessionModel.addBlock(typeId, Qt.point(40 + (index % 6) * 260, 40 + Math.floor(index / 6) * 200));
            if (id !== "") {
                made.push(id);
            }
            ++index;
        }
        ids = made;
        recordingPath = CheckEngine.addFakeRecording(SessionModel.sessionId, "Pseudo take", 5, false);
        CheckEngine.waitForFakeRecordings();
        // The library's note on a scan cut short is rendered too.
        CheckEngine.addFakeIncompletePlugins("clap", "/pseudo/many.clap");
    }

    function setRunning(running: bool) {
        if (AppShell.running !== running) {
            window.toolBar.runAction.trigger();
        }
    }

    function unfold(item: var) {
        if (item === null || item === undefined) {
            return;
        }
        if (item.toggle !== undefined && item.expanded !== undefined) {
            item.expanded = true;
        }
        for (const child of item.children ?? []) {
            unfold(child);
        }
    }

    function inject() {
        if (Inject === "hardcoded") {
            Qt.createQmlObject('import QtQuick; Text { objectName: "injected"; x: 10; y: 10; text: "Words nobody translated" }',
                               window.contentItem, "injected");
        } else if (Inject === "truncated") {
            Qt.createQmlObject('import QtQuick; import org.kde.ki18n; Text { objectName: "injected"; x: 10; y: 10; width: 30; elide: Text.ElideRight; '
                               + 'text: KI18n.i18nc("@title:window", "Delete Session") }', window.contentItem, "injected");
        } else if (Inject !== "") {
            fail("nothing to inject called " + Inject);
        }
    }

    function finishUp() {
        const summary = { screens: screens, elidedWithToolTip: elidedWithToolTip, findings: findings };
        if (!Checker.writeReport(JSON.stringify(summary, null, 1))) {
            fail("could not write the report");
            return;
        }
        console.log("pseudo check: " + screens + " screens, " + findings.length + " findings, "
                    + elidedWithToolTip + " elided texts readable in a tooltip");
        Checker.finish(findings.length === 0 ? 0 : 1);
    }

    Component.onCompleted: {
        Appearance.mode = AppearanceName.startsWith("reverblabr-") ? Appearance.ReverblabrDark : Appearance.FollowSystem;
        window = mainComponent.createObject(null, { width: 1280, height: 800 });
        if (window === null) {
            fail("the main window was not created");
            return;
        }
        window.show();
        step("a session", () => {}, () => AppShell.hasSession && BlockCatalogue.ready
                                           && SessionModel.sessionId === Sessions.currentId, 0);
        step("every block", buildScene, () => SessionModel.blockIds.length === ids.length && ids.length > 0, 300);
        step("injected", inject, null, 0);

        const whole = () => [rootOf(window)];
        step("blocks tab", () => { window.sidebarIndex = window.sidebar.blocksTab; unfold(window.blockLibrary); }, null, 0);
        screen("library", () => window, whole);
        step("running", () => {
            setRunning(true);
            for (const id of ids) {
                CheckEngine.setFakeLevel(SessionModel.sessionId, id, -12);
            }
        }, () => AppShell.running, 300);
        screen("running", () => window, whole);
        step("stopped", () => setRunning(false), () => !AppShell.running, 0);
        step("files tab", () => { window.sidebarIndex = window.sidebar.filesTab; }, null, 0);
        screen("files", () => window, whole);
        step("info tab", () => { window.sidebarIndex = window.sidebar.infoTab; }, null, 0);
        screen("info", () => window, whole);
        // **The Diagnostics section unfolded**, once the engine has
        // answered, with the section scrolled into the pane's view.
        step("diagnostics", () => { window.infoPane.diagnosticsSection.expanded = true; },
             () => Diagnostics.ready, 200);
        step("diagnostics in view", () => {
            const view = window.infoPane.contentItem;
            view.contentY = Math.max(0, view.contentHeight - view.height);
        }, null, 100);
        screen("info-diagnostics", () => window, whole);
        step("diagnostics folded", () => { window.infoPane.diagnosticsSection.expanded = false; }, null, 0);
        // **The session as a list**: every block's row, over the canvas.
        step("session as a list", () => { WindowState.listShown = true; },
             () => window.sessionCanvas.sessionList.visible, 300);
        screen("session-list", () => window, () => [window.sessionCanvas.sessionList]);
        step("the canvas again", () => { WindowState.listShown = false; }, null, 0);
        step("blocks tab again", () => { window.sidebarIndex = window.sidebar.blocksTab; }, null, 0);

        step("panels", () => {
            for (const id of ids) {
                const canvas = window.sessionCanvas;
                const typeName = SessionModel.blockType(id);
                step("panel " + typeName, () => { canvas.clearSelection(); canvas.closeSettings(); canvas.openSettings(id); },
                     () => window.settingsPanel.visible, 150);
                step("unfold " + typeName, () => unfold(window.settingsPanel), null, 200);
                screen("panel-" + typeName, () => window, () => [window.settingsPanel], 0);
            }
            step("panels closed", () => window.sessionCanvas.closeSettings(), null, 0);

            step("main menu", () => window.mainMenu.popup(window.contentItem, 0, 0), () => window.mainMenu.opened, 100);
            screen("menu", () => window, () => [window.mainMenu.contentItem], 0);
            for (let index = 0; index < window.mainMenu.count; ++index) {
                const menu = window.mainMenu.menuAt(index);
                if (menu === null) {
                    continue;
                }
                step("menu " + index, () => menu.open(), () => menu.opened, 100);
                screen("menu-" + index, () => window, () => [menu.contentItem], 0);
                step("menu " + index + " closed", () => menu.close(), null, 50);
            }
            step("main menu closed", () => window.mainMenu.close(), null, 50);

            for (const [name, dialog] of [["rename", window.renameSessionDialog], ["delete", window.deleteSessionDialog],
                                          ["open", window.openSessionDialog]]) {
                step("dialog " + name, () => dialog.open(), () => dialog.opened, 150);
                screen("dialog-" + name, () => window, () => [dialog.contentItem.parent], 0);
                step("dialog " + name + " closed", () => dialog.close(), () => !dialog.visible, 50);
            }

            step("virtual devices", () => window.showVirtualDevices(),
                 () => window.virtualDevicesWindow !== null && window.virtualDevicesWindow.visible, 300);
            screen("virtual-devices", () => window.virtualDevicesWindow,
                   () => [rootOf(window.virtualDevicesWindow)], 0);
            step("virtual devices closed", () => window.virtualDevicesWindow.close(), null, 50);

            step("sessions", () => window.showSessionsWindow(),
                 () => window.sessionsWindow !== null && window.sessionsWindow.visible, 300);
            screen("sessions", () => window.sessionsWindow, () => [rootOf(window.sessionsWindow)], 0);
            step("sessions closed", () => window.sessionsWindow.close(), null, 50);

            // The tray's popover, with a meter marked for it and
            // something gone wrong, so every part of it is on show.
            step("tray", () => {
                CheckEngine.applyEdit(SessionModel.sessionId, JSON.stringify({
                    op: "addBlock",
                    block: { id: "pseudoTray", type: "meter.peakrms", params: { showInTray: true } },
                    index: -1
                }));
                CheckEngine.reportError("plugin.crashed",
                                        JSON.stringify({ sessionId: SessionModel.sessionId, plugin: "Reverb" }));
                TrayState.requestPopover();
            }, () => window.trayPopover !== null && window.trayPopover.visible
                     && window.trayPopover.meterList.count > 0 && TrayState.attention, 300);
            screen("tray", () => window.trayPopover, () => [rootOf(window.trayPopover)], 0);
            step("tray closed", () => window.trayPopover.hide(), null, 50);

            let inspector = null;
            step("inspector", () => { inspector = window.openInspector(recordingPath); inspector.width = 820; inspector.height = 520; },
                 () => inspector !== null && inspector.inspector.loaded, 300);
            screen("inspector", () => inspector, () => [rootOf(inspector)], 0);
            step("inspector closed", () => inspector.close(), null, 50);

            // **Every block's help that is written, and one that is not**:
            // a page of text, which has to wrap rather than run out of its
            // column in a language 40 % longer.
            for (const typeId of Object.keys(BlockHelp.pages).concat(["effect.echo"])) {
                let help = null;
                step("help " + typeId, () => {
                    help = BlockHelp.show(typeId, window);
                    help.width = 600;
                    help.height = 900;
                }, () => help !== null && help.visible && help.typeId === typeId, 300);
                screen("help-" + typeId, () => help, () => [rootOf(help)], 0);
            }
            step("help closed", () => BlockHelp.window.close(), null, 50);
        }, null, 0);
        ticker.start();
    }
}

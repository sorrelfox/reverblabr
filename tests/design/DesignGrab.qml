// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import io.github.reverblabr

// Drives the real window through the scene's states and grabs each one.
// The scene comes from tests/design/design_compare.py, which
// renders the prototype in the same states; this writes the application's
// half: one PNG a screenshot, and a manifest of where each tile and the
// settings panel were, so the two can be cut out at the same place.
QtObject {
    id: driver

    /// The theme this run's appearance draws, which picks the screenshots
    /// it can take: the application does not switch appearance mid-run.
    readonly property string theme: AppearanceName.endsWith("light") ? "light" : "dark"

    property var window: null
    /// Block ids by the scene's name for them.
    property var ids: ({})
    /// The scene's recordings, name to path.
    property var recordingPaths: ({})
    property var manifest: ({})
    property var steps: []
    property int stepIndex: 0
    property double stepStarted: 0

    readonly property Component mainComponent: Component {
        Main {}
    }

    // Stepped by a timer rather than awaited: the window, the engine and
    // the meters all answer on the event loop.
    readonly property Timer ticker: Timer {
        interval: 50
        repeat: true
        onTriggered: driver.advance()
    }

    function fail(message: string) {
        console.error("design grab: " + message);
        Grabber.finish(1);
    }

    /// Adds a step: \a action runs once, then the next step waits until
    /// \a ready answers true and \a settleMs has passed.
    function step(label: string, action: var, ready: var, settleMs: int) {
        steps.push({ label: label, action: action, ready: ready || (() => true), settle: settleMs || 0, done: false });
    }

    function advance() {
        if (stepIndex >= steps.length) {
            ticker.stop();
            if (!Grabber.writeManifest(JSON.stringify(manifest, null, 1))) {
                fail("could not write the manifest");
                return;
            }
            Grabber.finish(0);
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

    /// Where \a item is in the window, as [x, y, width, height].
    function rectOf(item: var): var {
        const at = item.mapToItem(null, 0, 0);
        return [Math.round(at.x), Math.round(at.y), Math.round(item.width), Math.round(item.height)];
    }

    function tileFor(blockId: string): var {
        const tiles = window.sessionCanvas.tiles;
        for (let row = 0; row < tiles.count; ++row) {
            const tile = tiles.itemAt(row);
            if (tile !== null && tile.blockId === blockId) {
                return tile;
            }
        }
        return null;
    }

    function applyAppearance() {
        // **The application's own two**, picked as the Settings menu
        // picks them, before the window exists.
        //
        // **"system-dark" and "system-light" are the environment's**: the
        // application follows the system, and the comparison script hands
        // the process a colour scheme of its own to follow.
        if (AppearanceName === "reverblabr-dark") {
            Appearance.mode = Appearance.ReverblabrDark;
        } else if (AppearanceName === "reverblabr-light") {
            Appearance.mode = Appearance.ReverblabrLight;
        } else if (AppearanceName.startsWith("system-")) {
            Appearance.mode = Appearance.FollowSystem;
        } else {
            fail("the application has no appearance called " + AppearanceName);
        }
    }

    function buildScene() {
        SessionModel.renameSession(Scene.sessionName);
        const grid = Scene.grid;
        const made = {};
        for (const block of Scene.blocks) {
            made[block.name] = SessionModel.addBlock(block.type, Qt.point(grid.x + block.column * grid.column,
                                                                         grid.y + block.row * grid.row));
            // A block's settings, where a scene names any: the guide's
            // screenshot names the application its session plays.
            for (const param in (block.params ?? {})) {
                SessionModel.setParam(made[block.name], param, block.params[param]);
            }
        }
        ids = made;
        // A session's recordings, where a scene names any: real files
        // in the mock's own folder, filed in its own index, so the Files
        // tab has something to show.
        const paths = {};
        for (const recording of (Scene.recordings ?? [])) {
            paths[recording.name] = DesignEngine.addFakeRecording(SessionModel.sessionId, recording.name,
                                                                  recording.seconds, recording.inProgress ?? false);
        }
        recordingPaths = paths;
        DesignEngine.waitForFakeRecordings();
    }

    function wireScene() {
        const canvas = window.sessionCanvas;
        for (const wire of Scene.wires) {
            const from = ids[wire[0]], to = ids[wire[1]];
            const channels = Math.min(canvas.channelCountOf(from, true), canvas.channelCountOf(to, false));
            SessionModel.connectLanes(from, to, Math.max(1, channels));
        }
    }

    function setRunning(running: bool) {
        if (AppShell.running !== running) {
            window.toolBar.runAction.trigger();
        }
    }

    function giveSignal(running: bool) {
        DesignEngine.clearFakeLevels(SessionModel.sessionId);
        if (running) {
            for (const block of Scene.blocks) {
                DesignEngine.setFakeLevel(SessionModel.sessionId, ids[block.name], Scene.level);
            }
        }
    }

    /// Opens every folding section under \a item. **The prototype draws a
    /// panel with its sections open**, so a panel compared folded would be
    /// scored on what it hides rather than on how it draws what it shows.
    function unfold(item: var) {
        if (item === null) {
            return;
        }
        if (item.toggle !== undefined && item.expanded !== undefined) {
            item.expanded = true;
        }
        for (const child of item.children ?? []) {
            unfold(child);
        }
    }

    function measure(shot: var): var {
        const rects = { window: [0, 0, window.width, window.height] };
        for (const block of Scene.blocks) {
            const tile = tileFor(ids[block.name]);
            if (tile !== null) {
                rects["tile:" + block.name] = rectOf(tile);
            }
        }
        if (shot.panel !== null && window.settingsPanel.visible) {
            rects.panel = rectOf(window.settingsPanel);
        }
        // The session clock counts whatever is done, so it is left out.
        const masks = [];
        const clock = window.toolBar.clock;
        if (clock && clock.visible) {
            masks.push(rectOf(clock));
        }
        return { file: shot.name + ".png", rects: rects, masks: masks };
    }

    function takeShot(shot: var) {
        const canvas = window.sessionCanvas;
        step(shot.name + ": set up", () => {
            canvas.clearSelection();
            canvas.closeSettings();
            setRunning(shot.running);
            giveSignal(shot.running);
            if (shot.panel !== null) {
                canvas.openSettings(ids[shot.panel]);
            }
            // Which sidebar tab is in front, where a shot says: "files".
            if (shot.sidebar === "files") {
                window.sidebarIndex = window.sidebar.filesTab;
            } else if (shot.sidebar === "info") {
                window.sidebarIndex = window.sidebar.infoTab;
            } else {
                window.sidebarIndex = window.sidebar.blocksTab;
            }
        }, () => AppShell.running === shot.running
                  && (shot.panel === null || window.settingsPanel.visible)
                  && (!shot.running || Meters.live), 600);
        if (shot.panel !== null) {
            step(shot.name + ": unfold", () => unfold(window.settingsPanel), null, 300);
        }
        // **A Recording Inspector**, where a shot names a recording:
        // the window of its own is what is grabbed.
        if (shot.inspector !== undefined) {
            let inspector = null;
            step(shot.name + ": inspect", () => {
                inspector = window.openInspector(recordingPaths[shot.inspector]);
                inspector.width = shot.width ?? 820;
                inspector.height = shot.height ?? 520;
                if (shot.playAt !== undefined) {
                    RecordingPreview.seekTo(recordingPaths[shot.inspector], shot.playAt);
                }
            }, () => inspector !== null && inspector.inspector.loaded, 600);
            step(shot.name + ": grab", () => {
                if (shot.playAt !== undefined) {
                    RecordingPreview.pause();
                }
                if (!Grabber.grab(inspector, shot.name)) {
                    fail("could not grab " + shot.name);
                }
            }, null, 300);
            return;
        }
        // **A block's help**, where a shot names a block type: the help
        // window is what is grabbed.
        if (shot.help !== undefined) {
            let help = null;
            step(shot.name + ": help", () => {
                help = BlockHelp.show(shot.help, window);
                help.width = shot.width ?? 600;
                help.height = shot.height ?? 820;
            }, () => help !== null && help.visible && help.typeId === shot.help, 300);
            step(shot.name + ": grab", () => {
                if (!Grabber.grab(help, shot.name)) {
                    fail("could not grab " + shot.name);
                }
                help.close();
            }, null, 300);
            return;
        }
        step(shot.name + ": grab", () => {
            manifest[shot.name] = measure(shot);
            if (!Grabber.grab(window, shot.name)) {
                fail("could not grab " + shot.name);
            }
        });
    }

    Component.onCompleted: {
        applyAppearance();
        // **Drawn maximised**, as the prototype is scored: square,
        // with no border round it, whatever the offscreen platform makes of
        // the window's state.
        window = mainComponent.createObject(null, { width: Scene.window.width, height: Scene.window.height,
                                                    edgeToEdge: true });
        if (window === null) {
            fail("the main window was not created");
            return;
        }
        step("a session", () => {}, () => AppShell.hasSession && BlockCatalogue.ready
                                           && SessionModel.sessionId === Sessions.currentId, 0);
        step("the scene's blocks", buildScene, () => SessionModel.blockIds.length === Scene.blocks.length, 100);
        step("the scene's wires", wireScene, () => SessionModel.wires().length > 0, 200);
        for (const shot of Scene.screenshots) {
            if (shot.theme === theme) {
                takeShot(shot);
            }
        }
        step("stopped", () => setRunning(false), () => !AppShell.running, 0);
        ticker.start();
    }
}

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import QtQuick.Window
import QtTest
import io.github.reverblabr

// The Diagnostics section of the Info tab, against the mock engine:
// what it asks for and when, what it shows, and what Copy Diagnostics puts
// on the clipboard — the suite's own stand-in, never a desktop's.
TestCase {
    id: testCase

    name: "Diagnostics"

    /// What the mock is told to say: two sessions, one running without
    /// real-time priority and late three times, one stopped; two links; one
    /// recording format missing; five plugins, one of them left out.
    readonly property var fake: ({
            "sessions": [{
                    "id": "s1",
                    "name": "Morning call",
                    "state": "running",
                    "running": true,
                    "dspLoad": 0.12,
                    "peakDspLoad": 0.3,
                    "xruns": 3,
                    "lostCycles": 3,
                    "rate": 48000,
                    "quantum": 256,
                    "realtime": false,
                    "longRunsNotRealtime": 1,
                    "xrunHistory": [{
                            "at": "2026-09-28T10:00:00.000",
                            "total": 3,
                            "added": 3
                        }]
                }, {
                    "id": "s2",
                    "name": "Podcast",
                    "state": "idle",
                    "running": false,
                    "xrunHistory": []
                }],
            "graph": {
                "nodes": [{
                        "id": 40,
                        "name": "reverblabr.s1.seg0"
                    }],
                "ports": [],
                "links": [{
                        "from": "Firefox:output_FL",
                        "to": "reverblabr.s1.seg0:in_FL"
                    }, {
                        "from": "reverblabr.s1.seg0:out_FL",
                        "to": "Speakers:playback_FL"
                    }]
            },
            "recordingFormats": [{
                    "id": "wav",
                    "available": true
                }, {
                    "id": "aac",
                    "available": false,
                    "reason": "encoderMissing"
                }],
            "plugins": {
                "scanning": false,
                "formats": {
                    "lv2": 5
                },
                "total": 5,
                "blocked": [{
                        "format": "lv2",
                        "path": "/usr/lib64/lv2/broken.lv2",
                        "reason": "crashed"
                    }],
                "incomplete": [{
                        "format": "clap",
                        "path": "/usr/lib64/clap/many.clap"
                    }]
            }
        })

    // **In a shown window**: an item with no window is never visible, and
    // the section asks only while it is (and so does its warning show).
    Component {
        id: windowComponent

        Window {
            property alias section: section

            width: 480
            height: 800
            visible: true

            DiagnosticsSection {
                id: section

                width: parent.width
            }
        }
    }

    function makeSection(): var {
        const window = createTemporaryObject(windowComponent, testCase);
        return window.section;
    }

    function cleanup() {
        TestEngine.setFakeDiagnostics("");
        TestClipboard.clear();
    }

    function openSection(): var {
        TestEngine.setFakeDiagnostics(JSON.stringify(testCase.fake));
        const section = testCase.makeSection();
        section.expanded = true;
        tryVerify(() => Diagnostics.ready && Diagnostics.sessions.length === 2, 2000,
                  "the section asked when it opened, and the engine answered");
        return section;
    }

    function test_foldedItAsksNothing() {
        const section = testCase.makeSection();
        compare(section.expanded, false, "it starts folded");
        compare(section.asking, false, "and a folded section asks nothing");
        section.expanded = true;
        compare(section.asking, true, "open, it keeps asking");
        section.expanded = false;
        compare(section.asking, false, "and folded again it stops");
    }

    function test_itShowsHowEachSessionKeepsUp() {
        const section = testCase.openSection();
        tryCompare(section.sessionRepeater, "count", 2);
        const running = section.sessionRepeater.itemAt(0);
        const stopped = section.sessionRepeater.itemAt(1);
        verify(running.realtimeWarning.visible, "a session without real-time priority says so");
        verify(running.realtimeWarning.text.length > 0);
        verify(!stopped.realtimeWarning.visible, "a stopped session says nothing about real-time");
    }

    function test_itShowsTheGraphFormatsAndPlugins() {
        const section = testCase.openSection();
        tryCompare(section.linkRepeater, "count", 2);
        verify(section.formatsLabel.text.indexOf("aac") >= 0,
               "the missing format is named: " + section.formatsLabel.text);
        verify(section.pluginsLabel.text.indexOf("5") >= 0, section.pluginsLabel.text);
        verify(section.pluginsLabel.text.indexOf("1") >= 0, "and the one left out is counted: "
               + section.pluginsLabel.text);
        // A file the scan got only part of the way through.
        verify(section.incompleteLabel.visible, "a file only partly scanned is not said");
        verify(section.incompleteLabel.text.indexOf("1 file was only partly scanned") === 0,
               section.incompleteLabel.text);
    }

    function test_copyPutsTheBundleOnTheClipboard() {
        const section = testCase.openSection();
        tryVerify(() => section.copyButton.enabled);
        compare(TestClipboard.text, "");
        section.copyButton.clicked();
        const text = TestClipboard.text;
        verify(text.startsWith("Reverblabr diagnostics"), text.slice(0, 80));
        verify(text.indexOf("Morning call") >= 0, "the session is in it");
        verify(text.indexOf("real-time NO") >= 0, "and that it runs without real-time priority");
        verify(text.indexOf("late at 2026-09-28T10:00:00.000: +3") >= 0, "and when it was late");
        verify(text.indexOf("aac: no (encoderMissing)") >= 0, "the missing format, with why");
        verify(text.indexOf("Blocked: lv2") >= 0, "the plugin left out");
        verify(text.indexOf("Incomplete: clap") >= 0, "and the file only partly scanned");
        verify(text.indexOf("Firefox:output_FL") >= 0, "and our part of the graph");
    }
}

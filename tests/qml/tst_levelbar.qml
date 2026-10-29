// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The tray popover's level bars are read out as progress bars with a
// value: whole decibels between -60 and 0, brought up to date twice a
// second, and "Idle" when there is nothing to be loud.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "LevelBar"
    when: windowShown

    Component {
        id: shownBar

        Window {
            property alias bar: inner

            width: 240
            height: 40
            visible: true

            LevelBar {
                id: inner

                width: 200
                name: "Level of Evening"
            }
        }
    }

    /// A bar in a window that is showing, which is when it is read.
    function makeBar(properties) {
        const window = createTemporaryObject(shownBar, null);
        for (const key of Object.keys(properties)) {
            window.bar[key] = properties[key];
        }
        return window.bar;
    }

    function test_theLevelIsItsValueInWholeDecibels() {
        const bar = testCase.makeBar({db: -12.4});
        compare(bar.Accessible.role, Accessible.ProgressBar);
        compare(bar.Accessible.name, "Level of Evening");
        compare([bar.from, bar.to], [-60, 0]);
        tryCompare(bar, "value", -12, 1000, "the value is not the level");
        compare(bar.Accessible.description, "-12 dB");
    }

    function test_theValueStaysInsideTheBarsRange() {
        const bar = testCase.makeBar({db: -90});
        tryCompare(bar, "value", -60, 1000, "a level under the bar is not its bottom");
        bar.db = 3;
        tryCompare(bar, "value", 0, 1000, "a level over the bar is not its top");
    }

    function test_notRunningIsIdleNotSilent() {
        const bar = testCase.makeBar({db: -6, running: false});
        compare(bar.Accessible.description, "Idle");
        compare(bar.value, bar.from);
    }

    // **Twice a second, not every frame**: a screen reader given sixty
    // numbers a second says none of them.
    function test_theValueIsNotRewrittenEveryFrame() {
        const bar = testCase.makeBar({db: -20});
        tryCompare(bar, "value", -20, 1000);
        let changes = 0;
        bar.valueChanged.connect(() => ++changes);
        for (let frame = 0; frame < 30; ++frame) {
            bar.db = -20 - (frame % 10);
            wait(16);
        }
        verify(changes <= 2, "the value changed " + changes + " times in half a second");
    }
}

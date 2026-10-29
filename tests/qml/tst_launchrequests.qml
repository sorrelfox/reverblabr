// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// **What a launch asks for reaches the window**: the desktop entry's
// New Session and All Sessions, and a plain launch while Reverblabr runs,
// arrive as LaunchRequests.deliver() calls with the launch's command line,
// exactly as main() makes them.

import QtQuick
import QtTest
import io.github.reverblabr

TestCase {
    id: testCase

    name: "LaunchRequests"
    when: windowShown

    Component {
        id: mainWindow

        Main {}
    }

    function test_theWindowDoesWhatALaunchAsks() {
        const window = createTemporaryObject(mainWindow, testCase);
        verify(window, "the main window is created");
        tryVerify(() => AppShell.hasSession, 2000, "the window came up with a session");

        // **New Session**: a new session, in front, with the window hidden
        // in the tray brought back for it.
        TrayState.available = true;
        window.close();
        tryVerify(() => !window.visible, 1000);
        const before = Sessions.open.length;
        const front = Sessions.currentId;
        LaunchRequests.deliver(["reverblabr", "--new-session"]);
        tryVerify(() => Sessions.open.length === before + 1, 2000, "New Session opened no session");
        tryVerify(() => Sessions.currentId !== front, 2000, "the new session is not the one in front");
        verify(window.visible, "New Session left the window hidden");
        Sessions.closeSession(Sessions.currentId);
        tryVerify(() => Sessions.open.length === before, 2000);

        // **All Sessions**: the Sessions window.
        LaunchRequests.deliver(["reverblabr", "--sessions"]);
        tryVerify(() => window.sessionsWindow !== null && window.sessionsWindow.visible, 2000,
                  "All Sessions did not open the Sessions window");
        window.sessionsWindow.close();

        // **A plain launch** brings the window forward.
        window.close();
        tryVerify(() => !window.visible, 1000);
        LaunchRequests.deliver(["reverblabr"]);
        tryVerify(() => window.visible, 1000, "launching Reverblabr again left the window hidden");
        TrayState.available = false;
    }
}

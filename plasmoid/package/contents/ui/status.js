// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the widget says about the sessions in one line: its tooltip in the
// panel, and what a screen reader hears for its icon. Not a library, so the
// words come from the importing file's translation domain.

function summary(sessions) {
    // Nothing to say about a list that is gone, as it is while the widget
    // is taken down.
    if (!sessions) {
        return "";
    }
    if (!sessions.installed) {
        return i18nc("@info:status", "Reverblabr's background engine is not installed");
    }
    if (sessions.runningCount === 0) {
        return i18nc("@info:status", "No session is running");
    }
    if (sessions.recording) {
        return i18ncp("@info:status", "%1 session running, recording", "%1 sessions running, recording",
                      sessions.runningCount);
    }
    return i18ncp("@info:status", "%1 session running", "%1 sessions running", sessions.runningCount);
}

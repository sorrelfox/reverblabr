# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Recorder: a recording of the session, written to a file.

rb_add_block(recorder
    ENGINE
        recorder.cpp
        recorder.h
    UI
        ui/RecorderFace.qml
        ui/RecorderFileName.qml
        ui/RecorderHelp.qml
        ui/RecorderPanel.qml
        ui/RecorderWords.qml
    ICONS
        ui/output.recorder.svg
    TESTS
        tests/test_recorder.cpp
    QML_TESTS
        tests/tst_recorder.qml
)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Speech Now: shows a word and a dot while someone is speaking on this lane.

rb_add_block(speechnow
    ENGINE
        speechnow.cpp
        speechnow.h
    UI
        ui/SpeechNowDot.qml
        ui/SpeechNowFace.qml
        ui/SpeechNowHelp.qml
        ui/SpeechNowPanel.qml
        ui/SpeechNowState.qml
        ui/SpeechNowUi.qml
    ICONS
        ui/meter.speechnow.svg
    PRESETS
        presets/meter.speechnow/10-quick.json
        presets/meter.speechnow/20-steady.json
    TESTS
        tests/test_speechnow.cpp
    QML_TESTS
        tests/tst_speechnow.qml
)

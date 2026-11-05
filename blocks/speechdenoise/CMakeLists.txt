# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Speech Denoise: clears noise from speech with nothing to set up.

rb_add_block(speechdenoise
    REQUIRES RB_BUILD_SPEECH_DENOISE
    ENGINE
        speechdenoise.cpp
        speechdenoise.h
    UI
        ui/SpeechDenoiseFace.qml
        ui/SpeechDenoiseHelp.qml
        ui/SpeechDenoisePanel.qml
        ui/SpeechDenoiseState.qml
        ui/SpeechDenoiseUi.qml
    ICONS
        ui/advanced.speechdenoise.svg
    TESTS
        tests/test_speechdenoise.cpp
    QML_TESTS
        tests/tst_speechdenoise.qml
)

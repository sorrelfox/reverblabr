# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Voice Forward: moves voices forward against music and effects.

rb_add_block(voiceforward
    ENGINE
        voiceforward.cpp
        voiceforward.h
    UI
        ui/VoiceForwardFace.qml
        ui/VoiceForwardHelp.qml
        ui/VoiceForwardPanel.qml
        ui/VoiceForwardState.qml
        ui/VoiceForwardWords.qml
    ICONS
        ui/effect.voiceforward.svg
    PRESETS
        presets/effect.voiceforward/10-music-stays.json
        presets/effect.voiceforward/20-voices-first.json
    TESTS
        tests/test_voiceforward.cpp
    QML_TESTS
        tests/tst_voiceforward.qml
)

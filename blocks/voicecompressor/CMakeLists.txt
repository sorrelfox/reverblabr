# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Voice Compressor: holds a voice steady with three controls: compression, gate, ceiling.

rb_add_block(voicecompressor
    ENGINE
        voicecompressor.cpp
        voicecompressor.h
    UI
        ui/VoiceCompressorFace.qml
        ui/VoiceCompressorGraph.qml
        ui/VoiceCompressorHelp.qml
        ui/VoiceCompressorPanel.qml
        ui/VoiceCompressorState.qml
        ui/VoiceCompressorWords.qml
    ICONS
        ui/effect.voicecompressor.svg
    PRESETS
        presets/effect.voicecompressor/10-soft-voice.json
        presets/effect.voicecompressor/20-speech.json
        presets/effect.voicecompressor/30-strong-voice.json
        presets/effect.voicecompressor/40-singing.json
    TESTS
        tests/test_voicecompressor.cpp
    QML_TESTS
        tests/tst_voicecompressor.qml
)

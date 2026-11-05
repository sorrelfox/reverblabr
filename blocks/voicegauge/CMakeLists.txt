# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Voice Gauge: shows the level of a voice on this lane against marks you place.

rb_add_block(voicegauge
    ENGINE
        voicegauge.cpp
        voicegauge.h
    UI
        ui/VoiceGaugeBar.qml
        ui/VoiceGaugeDot.qml
        ui/VoiceGaugeFace.qml
        ui/VoiceGaugeHelp.qml
        ui/VoiceGaugePanel.qml
        ui/VoiceGaugeRow.qml
        ui/VoiceGaugeState.qml
        ui/VoiceGaugeWords.qml
    ICONS
        ui/meter.voicegauge.svg
    PRESETS
        presets/meter.voicegauge/10-quick.json
        presets/meter.voicegauge/20-steady.json
    TESTS
        tests/test_voicegauge.cpp
    QML_TESTS
        tests/tst_voicegauge.qml
)

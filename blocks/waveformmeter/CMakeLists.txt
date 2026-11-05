# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Waveform: draws the sound at this point in a lane as it goes by.

rb_add_block(waveformmeter
    ENGINE
        waveformmeter.cpp
        waveformmeter.h
    UI
        ui/scopeitem.cpp
        ui/scopeitem.h
        ui/WaveformFace.qml
        ui/WaveformHelp.qml
        ui/WaveformPanel.qml
        ui/WaveformScope.qml
        ui/WaveformState.qml
        ui/WaveformUi.qml
    ICONS
        ui/meter.waveform.svg
    PRESETS
        presets/meter.waveform/10-speaking.json
        presets/meter.waveform/20-singleTone.json
        presets/meter.waveform/30-longView.json
        presets/meter.waveform/40-leftAndRight.json
    TESTS
        tests/test_waveformmeter.cpp
    UI_TESTS
        tests/test_scopeitem.cpp
    QML_TESTS
        tests/tst_waveform.qml
)

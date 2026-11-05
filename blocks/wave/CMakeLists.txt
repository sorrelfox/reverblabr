# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Wave: a test tone or noise, on either side or both.

rb_add_block(wave
    ENGINE
        wave.cpp
        wave.h
    UI
        ui/WaveFace.qml
        ui/WaveHelp.qml
        ui/WavePanel.qml
        ui/WaveWords.qml
    ICONS
        ui/output.wave.svg
    TESTS
        tests/test_wave.cpp
    QML_TESTS
        tests/tst_wave.qml
)

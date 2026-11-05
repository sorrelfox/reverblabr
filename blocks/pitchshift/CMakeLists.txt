# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Pitch Shift: moves the pitch up or down, at the same speed.

rb_add_block(pitchshift
    ENGINE
        pitchshift.cpp
        pitchshift.h
    UI
        ui/PitchShiftFace.qml
        ui/PitchShiftHelp.qml
        ui/PitchShiftPanel.qml
        ui/PitchShiftUi.qml
        ui/PitchShiftWords.qml
    ICONS
        ui/effect.pitchshift.svg
    TESTS
        tests/test_pitchshift.cpp
    QML_TESTS
        tests/tst_pitchshift.qml
)

# Pitch Shift moves the pitch with SoundTouch.
pkg_check_modules(SOUNDTOUCH REQUIRED IMPORTED_TARGET soundtouch>=2.1)
target_link_libraries(reverblabr-blocks PRIVATE PkgConfig::SOUNDTOUCH)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Hearing Compensation 64: lifts quiet sound for one ear, in sixty-four bands set with points.

rb_add_block(hearingcompensation
    ENGINE
        hearingcompensation.cpp
        hearingcompensation.h
    UI
        ui/HearingBands.qml
        ui/HearingCompensation16Help.qml
        ui/HearingCompensation16Ui.qml
        ui/HearingCompensation64Help.qml
        ui/HearingCompensation64Ui.qml
        ui/HearingCompensationFace.qml
        ui/HearingCompensationPanel.qml
    ICONS
        ui/hearing.compensation16.svg
        ui/hearing.compensation64.svg
    PRESETS
        presets/hearing.compensation16/010-flat.json
        presets/hearing.compensation16/020-gentleSlope.json
        presets/hearing.compensation16/030-steeperSlope.json
        presets/hearing.compensation64/010-flat.json
        presets/hearing.compensation64/020-gentleSlope.json
        presets/hearing.compensation64/030-steeperSlope.json
    TESTS
        tests/test_hearingcompensation.cpp
        tests/test_hearingcompensation64.cpp
    QML_TESTS
        tests/tst_hearingcompensation.qml
        tests/tst_hearingcompensation64.qml
)

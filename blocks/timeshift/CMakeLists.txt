# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Time Shift: pauses and rewinds sound that is still arriving.

rb_add_block(timeshift
    ENGINE
        timeshift.cpp
        timeshift.h
        timeshiftspill.cpp
        timeshiftspill.h
    UI
        ui/TimeShiftFace.qml
        ui/TimeShiftHelp.qml
        ui/TimeShiftPanel.qml
        ui/TimeShiftState.qml
        ui/TimeShiftUi.qml
    ICONS
        ui/advanced.timeshift.svg
    TESTS
        tests/test_timeshift.cpp
    QML_TESTS
        tests/tst_timeshift.qml
)

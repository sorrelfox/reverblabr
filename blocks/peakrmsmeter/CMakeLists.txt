# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Peak/RMS: a level meter for any point in a lane.

rb_add_block(peakrmsmeter
    ENGINE
        peakrmsmeter.cpp
        peakrmsmeter.h
    UI
        ui/PeakRmsFace.qml
        ui/PeakRmsHelp.qml
        ui/PeakRmsPanel.qml
        ui/PeakRmsUi.qml
    ICONS
        ui/meter.peakrms.svg
    TESTS
        tests/test_peakrmsmeter.cpp
    QML_TESTS
        tests/tst_peakrmspanel.qml
)

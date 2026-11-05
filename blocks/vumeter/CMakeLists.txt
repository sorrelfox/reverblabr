# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# VU: a pair of needles following the average level.

rb_add_block(vumeter
    ENGINE
        vumeter.cpp
        vumeter.h
    UI
        ui/VuFace.qml
        ui/VuGauge.qml
        ui/VuHelp.qml
        ui/VuPanel.qml
        ui/vuscale.cpp
        ui/vuscale.h
        ui/VuUi.qml
    ICONS
        ui/meter.vu.svg
    TESTS
        tests/test_vumeter.cpp
    UI_TESTS
        tests/test_vuscale.cpp
    QML_TESTS
        tests/tst_vumeter.qml
)

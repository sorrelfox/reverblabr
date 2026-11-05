# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# FIR Filter: applies a correction measured from a file.

rb_add_block(firfilter
    ENGINE
        firfilter.cpp
        firfilter.h
    UI
        ui/FirFilterFace.qml
        ui/FirFilterHelp.qml
        ui/FirFilterPanel.qml
        ui/FirFilterState.qml
        ui/FirFilterUi.qml
    ICONS
        ui/advanced.firfilter.svg
    TESTS
        tests/test_firfilter.cpp
    QML_TESTS
        tests/tst_firfilter.qml
)

# The FIR filter reads its impulse response from a sound file.
target_link_libraries(reverblabr-blocks PRIVATE reverblabr-record)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# High Pass Filter: rolls off everything below a chosen frequency.
# Low Pass Filter: rolls off everything above a chosen frequency.

rb_add_block(cutofffilter
    ENGINE
        cutofffilter.cpp
        cutofffilter.h
        highpassfilter.h
        lowpassfilter.h
    UI
        ui/CutoffFace.qml
        ui/CutoffPanel.qml
        ui/HighPassHelp.qml
        ui/LowPassHelp.qml
    ICONS
        ui/effect.highpass.svg
        ui/effect.lowpass.svg
    TESTS
        tests/test_highpassfilter.cpp
        tests/test_lowpassfilter.cpp
    QML_TESTS
        tests/tst_highpassfilter.qml
        tests/tst_lowpassfilter.qml
)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Pan: places a sound between the left and the right.

rb_add_block(pan
    ENGINE
        pan.cpp
        pan.h
    UI
        ui/PanHelp.qml
    ICONS
        ui/effect.pan.svg
    TESTS
        tests/test_pan.cpp
    QML_TESTS
        tests/tst_pan.qml
)

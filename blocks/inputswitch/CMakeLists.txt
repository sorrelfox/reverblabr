# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Input Switch: passes one of two inputs and fades between them.

rb_add_block(inputswitch
    ENGINE
        inputswitch.cpp
        inputswitch.h
    UI
        ui/InputSwitchControl.qml
        ui/InputSwitchFace.qml
        ui/InputSwitchHelp.qml
        ui/InputSwitchPanel.qml
        ui/InputSwitchUi.qml
    ICONS
        ui/advanced.inputswitch.svg
    TESTS
        tests/test_inputswitch.cpp
    QML_TESTS
        tests/tst_inputswitch.qml
)

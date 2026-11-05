# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Input Device: a microphone or other recording input.

rb_add_block(inputdevice
    ENGINE
        inputdevice.cpp
        inputdevice.h
    UI
        ui/InputDeviceHelp.qml
        ui/InputDevicePanel.qml
        ui/InputDeviceUi.qml
    ICONS
        ui/source.device.svg
    TESTS
        tests/test_inputdevice.cpp
)

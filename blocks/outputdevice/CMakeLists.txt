# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Output Device: speakers, headphones or another output.

rb_add_block(outputdevice
    ENGINE
        outputdevice.cpp
        outputdevice.h
    UI
        ui/OutputDeviceHelp.qml
        ui/OutputDevicePanel.qml
        ui/OutputDeviceUi.qml
    ICONS
        ui/output.device.svg
    TESTS
        tests/test_outputdevice.cpp
)

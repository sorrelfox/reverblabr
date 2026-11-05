# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Echo: repeats the sound, each repeat quieter than the last.

rb_add_block(echo
    ENGINE
        echo.cpp
        echo.h
    UI
        ui/EchoFace.qml
        ui/EchoHelp.qml
        ui/EchoPanel.qml
        ui/EchoUi.qml
    ICONS
        ui/effect.echo.svg
    TESTS
        tests/test_echo.cpp
    QML_TESTS
        tests/tst_echo.qml
)

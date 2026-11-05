# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Sudden Sounds: softens sudden sharp sounds such as clicks and knocks.

rb_add_block(suddensounds
    ENGINE
        suddensounds.cpp
        suddensounds.h
    UI
        ui/SuddenSoundsFace.qml
        ui/SuddenSoundsHelp.qml
        ui/SuddenSoundsPanel.qml
        ui/SuddenSoundsState.qml
        ui/SuddenSoundsUi.qml
        ui/SuddenSoundsWords.qml
    ICONS
        ui/hearing.suddensounds.svg
    TESTS
        tests/test_suddensounds.cpp
    QML_TESTS
        tests/tst_suddensounds.qml
)

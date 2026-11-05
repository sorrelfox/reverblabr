# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Ducking: turns a background source down under a voice.

rb_add_block(ducking
    ENGINE
        ducking.cpp
        ducking.h
    UI
        ui/DuckingFace.qml
        ui/DuckingHelp.qml
        ui/DuckingPanel.qml
        ui/DuckingWords.qml
    ICONS
        ui/advanced.ducking.svg
    TESTS
        tests/test_ducking.cpp
    QML_TESTS
        tests/tst_ducking.qml
)

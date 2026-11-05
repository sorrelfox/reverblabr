# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Declick: takes out clicks, pops and crackle.

rb_add_block(declick
    ENGINE
        declick.cpp
        declick.h
    UI
        ui/DeclickFace.qml
        ui/DeclickHelp.qml
        ui/DeclickPanel.qml
        ui/DeclickUi.qml
        ui/DeclickWords.qml
    ICONS
        ui/advanced.declick.svg
    TESTS
        tests/test_declick.cpp
    QML_TESTS
        tests/tst_declick.qml
)

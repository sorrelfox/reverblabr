# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Dehum: takes out the steady hum of the mains supply.

rb_add_block(dehum
    ENGINE
        dehum.cpp
        dehum.h
    UI
        ui/DehumFace.qml
        ui/DehumHelp.qml
        ui/DehumPanel.qml
        ui/DehumWords.qml
    ICONS
        ui/advanced.dehum.svg
    PRESETS
        presets/advanced.dehum/10-mains50Hz.json
        presets/advanced.dehum/20-mains60Hz.json
    TESTS
        tests/test_dehum.cpp
    QML_TESTS
        tests/tst_dehum.qml
)

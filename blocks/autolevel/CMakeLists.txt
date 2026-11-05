# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Auto Level: evens out a source whose loudness keeps changing.

rb_add_block(autolevel
    ENGINE
        autolevel.cpp
        autolevel.h
    UI
        ui/AutoLevelFace.qml
        ui/AutoLevelGraph.qml
        ui/AutoLevelHelp.qml
        ui/AutoLevelPanel.qml
        ui/AutoLevelState.qml
    ICONS
        ui/effect.autolevel.svg
    PRESETS
        presets/effect.autolevel/10-voiceEvener.json
    TESTS
        tests/test_autolevel.cpp
    QML_TESTS
        tests/tst_autolevel.qml
)

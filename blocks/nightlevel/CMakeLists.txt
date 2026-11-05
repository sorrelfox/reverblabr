# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

rb_add_block(nightlevel
    ENGINE
        nightlevel.cpp
        nightlevel.h
    UI
        ui/NightLevelFace.qml
        ui/NightLevelGraph.qml
        ui/NightLevelHelp.qml
        ui/NightLevelPanel.qml
        ui/NightLevelState.qml
        ui/NightLevelWords.qml
    ICONS
        ui/effect.nightlevel.svg
    PRESETS
        presets/effect.nightlevel/10-light.json
        presets/effect.nightlevel/20-night.json
        presets/effect.nightlevel/30-strong.json
    TESTS
        tests/test_nightlevel.cpp
    QML_TESTS
        tests/tst_nightlevel.qml
)

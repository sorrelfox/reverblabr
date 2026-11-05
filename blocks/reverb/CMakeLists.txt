# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Reverb: puts the sound in a room, from a booth to a hall.

rb_add_block(reverb
    ENGINE
        reverb.cpp
        reverb.h
    UI
        ui/ReverbFace.qml
        ui/ReverbHelp.qml
        ui/ReverbPanel.qml
        ui/ReverbWords.qml
    ICONS
        ui/effect.reverb.svg
    PRESETS
        presets/effect.reverb/10-smallRoom.json
        presets/effect.reverb/20-hall.json
        presets/effect.reverb/30-plate.json
    TESTS
        tests/test_reverb.cpp
    QML_TESTS
        tests/tst_reverb.qml
)

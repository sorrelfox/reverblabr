# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Sound Side: shows which side of the mix is louder right now.

rb_add_block(soundside
    ENGINE
        soundside.cpp
        soundside.h
    UI
        ui/SoundSideFace.qml
        ui/SoundSideHelp.qml
        ui/SoundSideOverlay.qml
        ui/SoundSidePanel.qml
        ui/SoundSideState.qml
        ui/SoundSideStrip.qml
        ui/SoundSideUi.qml
        ui/SoundSideWords.qml
    ICONS
        ui/meter.soundside.svg
    PRESETS
        presets/meter.soundside/10-wholeMix.json
        presets/meter.soundside/20-higherSounds.json
    TESTS
        tests/test_soundside.cpp
    QML_TESTS
        tests/tst_soundside.qml
)

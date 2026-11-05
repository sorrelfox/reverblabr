# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Volume: turns a source up or down to match the others.

rb_add_block(volume
    ENGINE
        volume.cpp
        volume.h
    UI
        ui/VolumeFace.qml
        ui/VolumeHelp.qml
        ui/VolumePanel.qml
    ICONS
        ui/effect.volume.svg
    PRESETS
        presets/effect.volume/10-halfVolume.json
        presets/effect.volume/20-unityVolume.json
        presets/effect.volume/30-doubleVolume.json
    TESTS
        tests/test_volume.cpp
    QML_TESTS
        tests/tst_volumepanel.qml
)

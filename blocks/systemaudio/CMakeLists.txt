# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# System Audio: everything this machine is playing.

rb_add_block(systemaudio
    ENGINE
        systemaudio.cpp
        systemaudio.h
    UI
        ui/SystemAudioFace.qml
        ui/SystemAudioHelp.qml
        ui/SystemAudioPanel.qml
    ICONS
        ui/source.system.svg
    TESTS
        tests/test_systemaudio.cpp
    QML_TESTS
        tests/tst_systemaudio.qml
)

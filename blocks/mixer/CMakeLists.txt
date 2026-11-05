# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Mixer: combines up to five sources, each with its own strip.

rb_add_block(mixer
    ENGINE
        mixer.cpp
        mixer.h
    UI
        ui/MixerFace.qml
        ui/MixerHelp.qml
        ui/MixerPanel.qml
        ui/MixerStrip.qml
    ICONS
        ui/advanced.mixer.svg
    TESTS
        tests/test_mixer.cpp
    QML_TESTS
        tests/tst_mixer.qml
)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Denoise: takes out a steady noise once it has been learned.

rb_add_block(denoise
    ENGINE
        denoise.cpp
        denoise.h
    UI
        ui/DenoiseFace.qml
        ui/DenoiseHelp.qml
        ui/DenoisePanel.qml
        ui/DenoiseState.qml
    ICONS
        ui/advanced.denoise.svg
    TESTS
        tests/test_denoise.cpp
    QML_TESTS
        tests/tst_denoise.qml
)

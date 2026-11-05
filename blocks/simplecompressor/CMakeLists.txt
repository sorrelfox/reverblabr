# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Simple Compressor: steadies the level from one choice, not six controls.

rb_add_block(simplecompressor
    ENGINE
        simplecompressor.cpp
        simplecompressor.h
    UI
        ui/SimpleCompressorFace.qml
        ui/SimpleCompressorHelp.qml
        ui/SimpleCompressorPanel.qml
        ui/SimpleCompressorUi.qml
    ICONS
        ui/effect.simplecompressor.svg
    TESTS
        tests/test_simplecompressor.cpp
    QML_TESTS
        tests/tst_simplecompressor.qml
)

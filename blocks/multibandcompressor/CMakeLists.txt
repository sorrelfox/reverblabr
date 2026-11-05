# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Multiband Compressor: steadies the low, middle and high ranges apart.

rb_add_block(multibandcompressor
    ENGINE
        multibandcompressor.cpp
        multibandcompressor.h
    UI
        ui/MultibandCompressorFace.qml
        ui/MultibandCompressorHelp.qml
        ui/MultibandCompressorPanel.qml
        ui/MultibandWords.qml
    ICONS
        ui/effect.multibandcompressor.svg
    TESTS
        tests/test_multibandcompressor.cpp
    QML_TESTS
        tests/tst_multibandcompressor.qml
)

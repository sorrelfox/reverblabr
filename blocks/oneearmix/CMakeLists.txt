# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# One Ear Mix: a mono mix that puts both sides into one ear.

rb_add_block(oneearmix
    ENGINE
        oneearmix.cpp
        oneearmix.h
    UI
        ui/OneEarMixFace.qml
        ui/OneEarMixHelp.qml
        ui/OneEarMixPanel.qml
        ui/OneEarMixState.qml
        ui/OneEarMixWords.qml
    ICONS
        ui/hearing.oneearmix.svg
    TESTS
        tests/test_oneearmix.cpp
    QML_TESTS
        tests/tst_oneearmix.qml
)

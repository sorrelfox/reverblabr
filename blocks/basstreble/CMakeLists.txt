# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Bass & Treble: lifts or cuts the low and the high end.

rb_add_block(basstreble
    ENGINE
        basstreble.cpp
        basstreble.h
    UI
        ui/BassTrebleFace.qml
        ui/BassTrebleHelp.qml
        ui/BassTreblePanel.qml
        ui/BassTrebleUi.qml
    ICONS
        ui/effect.basstreble.svg
    TESTS
        tests/test_basstreble.cpp
    QML_TESTS
        tests/tst_basstreble.qml
)

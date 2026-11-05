# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Frequency Lowering: moves high sounds down to where one ear hears them.
# Frequency Raising: moves low sounds up to where one ear hears them.

rb_add_block(frequencymove
    ENGINE
        frequencylowering.cpp
        frequencylowering.h
        frequencymoveblock.cpp
        frequencymoveblock.h
        frequencyraising.cpp
        frequencyraising.h
    UI
        ui/FrequencyLoweringFace.qml
        ui/FrequencyLoweringHelp.qml
        ui/FrequencyLoweringPanel.qml
        ui/FrequencyMoveFace.qml
        ui/FrequencyMovePanel.qml
        ui/FrequencyMoves.qml
        ui/FrequencyRaisingFace.qml
        ui/FrequencyRaisingHelp.qml
        ui/FrequencyRaisingPanel.qml
    ICONS
        ui/hearing.frequencylowering.svg
        ui/hearing.frequencyraising.svg
    TESTS
        tests/test_frequencylowering.cpp
        tests/test_frequencyraising.cpp
    QML_TESTS
        tests/tst_frequencylowering.qml
        tests/tst_frequencyraising.qml
)

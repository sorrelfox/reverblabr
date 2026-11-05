# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Mono Enhancer: widens a mono recording without breaking it.

rb_add_block(monoenhancer
    ENGINE
        monoenhancer.cpp
        monoenhancer.h
    UI
        ui/MonoEnhancerFace.qml
        ui/MonoEnhancerHelp.qml
        ui/MonoEnhancerPanel.qml
        ui/MonoEnhancerUi.qml
    ICONS
        ui/effect.monoenhancer.svg
    TESTS
        tests/test_monoenhancer.cpp
    QML_TESTS
        tests/tst_monoenhancer.qml
)

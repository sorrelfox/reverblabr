# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Parametric EQ: shapes the tone with bands you place yourself.

rb_add_block(parametriceq
    ENGINE
        parametriceq.cpp
        parametriceq.h
    UI
        ui/equalizerplot.cpp
        ui/equalizerplot.h
        ui/ParametricEqBands.qml
        ui/ParametricEqFace.qml
        ui/ParametricEqHelp.qml
        ui/ParametricEqPanel.qml
        ui/ParametricEqUi.qml
    ICONS
        ui/advanced.parametriceq.svg
    TESTS
        tests/test_parametriceq.cpp
    UI_TESTS
        tests/test_equalizerplot.cpp
    QML_TESTS
        tests/tst_parametriceq.qml
)

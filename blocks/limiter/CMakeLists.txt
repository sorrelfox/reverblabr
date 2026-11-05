# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Limiter: holds the signal below a ceiling it cannot cross.

rb_add_block(limiter
    ENGINE
        limiter.cpp
        limiter.h
    UI
        ui/LimiterFace.qml
        ui/LimiterHelp.qml
        ui/LimiterPanel.qml
        ui/LimiterUi.qml
        ui/LimiterWords.qml
    ICONS
        ui/effect.limiter.svg
    TESTS
        tests/test_limiter.cpp
    QML_TESTS
        tests/tst_limiter.qml
)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Balance: trims one side of a stereo signal against the other.

rb_add_block(balance
    ENGINE
        balance.cpp
        balance.h
    UI
        ui/BalanceHelp.qml
    ICONS
        ui/effect.balance.svg
    TESTS
        tests/test_balance.cpp
    QML_TESTS
        tests/tst_balance.qml
)

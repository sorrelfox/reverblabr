# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Join L/R: puts a left and a right lane back into one stereo pair.

rb_add_block(joinlr
    ENGINE
        joinlr.cpp
        joinlr.h
    UI
        ui/JoinLrHelp.qml
    ICONS
        ui/hearing.joinlr.svg
    TESTS
        tests/test_joinlr.cpp
)

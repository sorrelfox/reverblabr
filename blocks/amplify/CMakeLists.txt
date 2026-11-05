# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Amplify: makes a source louder, up to ten times, and nothing else.

rb_add_block(amplify
    ENGINE
        amplify.cpp
        amplify.h
    UI
        ui/AmplifyFace.qml
        ui/AmplifyHelp.qml
        ui/AmplifyPanel.qml
        ui/AmplifyUi.qml
    ICONS
        ui/effect.amplify.svg
    TESTS
        tests/test_amplify.cpp
    QML_TESTS
        tests/tst_amplify.qml
)

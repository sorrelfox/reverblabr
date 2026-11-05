# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Channels: swaps, mutes or folds the left and right channels.

rb_add_block(channels
    ENGINE
        channels.cpp
        channels.h
    UI
        ui/ChannelsFace.qml
        ui/ChannelsHelp.qml
        ui/ChannelsPanel.qml
        ui/ChannelsUi.qml
        ui/ChannelsWords.qml
    ICONS
        ui/effect.channels.svg
    TESTS
        tests/test_channels.cpp
    QML_TESTS
        tests/tst_channels.qml
)

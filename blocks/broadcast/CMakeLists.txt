# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Broadcast: a stream for an internet radio server.
# Live Stream: a video stream for a streaming platform.

rb_add_block(broadcast
    REQUIRES RB_BUILD_BROADCAST
    ENGINE
        broadcast.cpp
        broadcast.h
        streamtap.cpp
        streamtap.h
    UI
        ui/BroadcastHelp.qml
        ui/BroadcastPanel.qml
        ui/broadcaststatus.cpp
        ui/broadcaststatus.h
        ui/BroadcastWords.qml
        ui/mediaplayers.cpp
        ui/mediaplayers.h
        ui/NowPlayingRows.qml
    ICONS
        ui/output.broadcast.svg
    TESTS
        tests/test_broadcast.cpp
    QML_TESTS
        tests/tst_broadcast.qml
)
rb_add_block(livestream
    REQUIRES RB_BUILD_LIVESTREAM
    ENGINE
        livestream.cpp
        livestream.h
    UI
        ui/LiveStreamHelp.qml
        ui/LiveStreamPanel.qml
        ui/LiveStreamWords.qml
    ICONS
        ui/output.livestream.svg
    TESTS
        tests/test_livestream.cpp
    QML_TESTS
        tests/tst_livestream.qml
)

if(RB_BUILD_BROADCAST)
    target_compile_definitions(reverblabr-blocks PUBLIC RB_HAVE_BROADCAST)
endif()
if(RB_BUILD_LIVESTREAM)
    target_compile_definitions(reverblabr-blocks PUBLIC RB_HAVE_LIVESTREAM)
endif()

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Sync: delays a lane to line it up with another.

rb_add_block(sync
    ENGINE
        sync.cpp
        sync.h
    UI
        ui/SyncFace.qml
        ui/SyncHelp.qml
        ui/SyncPanel.qml
        ui/SyncScale.qml
        ui/SyncUi.qml
    ICONS
        ui/advanced.sync.svg
    TESTS
        tests/test_sync.cpp
    QML_TESTS
        tests/tst_sync.qml
)

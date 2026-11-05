# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Application: sound from one application.

rb_add_block(applicationsource
    ENGINE
        applicationsource.cpp
        applicationsource.h
    UI
        ui/ApplicationSourceFace.qml
        ui/ApplicationSourceHelp.qml
        ui/ApplicationSourcePanel.qml
    ICONS
        ui/source.application.svg
    TESTS
        tests/test_applicationsource.cpp
    QML_TESTS
        tests/tst_applicationsource.qml
)

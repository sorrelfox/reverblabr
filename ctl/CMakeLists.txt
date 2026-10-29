# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# reverblabr-ctl: the engine from a terminal or a script. A client of
# the public D-Bus API (docs/dbus-api.md): QtCore and QtDBus, and gettext
# from the C library for its words. No GUI, no KDE Frameworks, no engine.

# Everything but the command line: the words, finding a session or a block
# by what a person typed, reading a value for a setting, and asking the
# engine and waiting for its answer. The tests drive a daemon with the same
# client the tool uses.
add_library(reverblabr-ctl-core STATIC
    engineclient.cpp
    engineclient.h
    failure.h
    lookup.cpp
    lookup.h
    translate.cpp
    translate.h
)
target_include_directories(reverblabr-ctl-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-ctl-core PUBLIC Qt6::Core Qt6::DBus)
target_compile_definitions(reverblabr-ctl-core PRIVATE RB_CTL_LOCALEDIR="${KDE_INSTALL_FULL_LOCALEDIR}")
set_target_properties(reverblabr-ctl-core PROPERTIES AUTOMOC ON AUTORCC OFF)

add_executable(reverblabr-ctl
    commands.cpp
    commands.h
    main.cpp
)
target_link_libraries(reverblabr-ctl PRIVATE reverblabr-ctl-core reverblabr-version Qt6::Core Qt6::DBus)
set_target_properties(reverblabr-ctl PROPERTIES AUTOMOC OFF AUTORCC OFF)

install(TARGETS reverblabr-ctl ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES reverblabr-ctl.1 DESTINATION ${KDE_INSTALL_MANDIR}/man1)
# bash-completion loads a command's file by the command's name.
install(FILES completions/reverblabr-ctl.bash
        DESTINATION ${KDE_INSTALL_DATADIR}/bash-completion/completions
        RENAME reverblabr-ctl)
install(FILES completions/_reverblabr-ctl DESTINATION ${KDE_INSTALL_ZSHAUTOCOMPLETEDIR})

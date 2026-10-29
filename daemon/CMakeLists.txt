# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# reverblabrd: the engine as a service on the session bus (ADR-0017). The
# engine, PipeWire, QtCore and QtDBus; no GUI and no KDE Frameworks.
# Auto Run (ADR-0021): which sessions start with the engine, and the
# user unit enabled while there are any. The timers' systemd user units,
# kept in step with the sessions' timers. A library of its own so the unit
# tests can drive it against the mock engine and a fake service manager.
# The sessions in KRunner (ADR-0023) are here too: the runner the daemon
# serves, with its words from the notifications' catalog.
add_library(reverblabr-daemon-core STATIC
    autorun.cpp
    autorun.h
    autostartunit.cpp
    autostartunit.h
    sessionrunner.cpp
    sessionrunner.h
    timerschedule.cpp
    timerschedule.h
    timerunits.cpp
    timerunits.h
)
target_include_directories(reverblabr-daemon-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-daemon-core PUBLIC reverblabr-engine reverblabr-notify Qt6::Core Qt6::DBus)
set_target_properties(reverblabr-daemon-core PROPERTIES AUTOMOC ON AUTORCC OFF)

add_executable(reverblabrd
    main.cpp
    opensessions.cpp
    opensessions.h
)
target_link_libraries(reverblabrd PRIVATE reverblabr-version reverblabr-daemon-core reverblabr-enginebus reverblabr-engine reverblabr-blocks reverblabr-notify Qt6::Core Qt6::DBus)
set_target_properties(reverblabrd PROPERTIES AUTOMOC ON AUTORCC OFF)
# The window a KRunner match opens a session in.
target_compile_definitions(reverblabrd PRIVATE RB_WINDOW_EXEC="${KDE_INSTALL_FULL_BINDIR}/reverblabr")
install(TARGETS reverblabrd ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# The activation file and the user unit, both naming the installed daemon.
set(RB_DAEMON_EXEC "${KDE_INSTALL_FULL_BINDIR}/reverblabrd")
configure_file(io.github.reverblabr.Engine.service.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Engine.service @ONLY)
configure_file(reverblabrd.service.in ${CMAKE_CURRENT_BINARY_DIR}/reverblabrd.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Engine.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/reverblabrd.service DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})

# **KRunner's runner file**: KRunner reads every file in this folder and
# calls the runner it names on the bus, which starts the daemon by its name.
# Installed with its name and comment translated.
rb_translate_data_file(reverblabrd DESKTOP
    ${CMAKE_CURRENT_SOURCE_DIR}/io.github.reverblabr.Runner.desktop ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Runner.desktop)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Runner.desktop DESTINATION ${KDE_INSTALL_DATADIR}/krunner/dbusplugins)

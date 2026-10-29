# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# reverblabrd: the engine as a service on the session bus (ADR-0017). The
# engine, PipeWire, QtCore and QtDBus; no GUI and no KDE Frameworks.
# Auto Run (ADR-0021): which sessions start with the engine, and the
# user unit enabled while there are any. A library of its own so the unit
# tests can drive it against the mock engine and a fake service manager.
add_library(reverblabr-daemon-core STATIC
    autorun.cpp
    autorun.h
    autostartunit.cpp
    autostartunit.h
)
target_include_directories(reverblabr-daemon-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-daemon-core PUBLIC reverblabr-engine Qt6::Core Qt6::DBus)
set_target_properties(reverblabr-daemon-core PROPERTIES AUTOMOC ON AUTORCC OFF)

add_executable(reverblabrd
    main.cpp
    opensessions.cpp
    opensessions.h
)
target_link_libraries(reverblabrd PRIVATE reverblabr-version reverblabr-daemon-core reverblabr-enginebus reverblabr-engine reverblabr-blocks reverblabr-notify Qt6::Core Qt6::DBus)
set_target_properties(reverblabrd PROPERTIES AUTOMOC ON AUTORCC OFF)
install(TARGETS reverblabrd ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# The activation file and the user unit, both naming the installed daemon.
set(RB_DAEMON_EXEC "${KDE_INSTALL_FULL_BINDIR}/reverblabrd")
configure_file(io.github.reverblabr.Engine.service.in ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Engine.service @ONLY)
configure_file(reverblabrd.service.in ${CMAKE_CURRENT_BINARY_DIR}/reverblabrd.service @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Engine.service DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/reverblabrd.service DESTINATION ${KDE_INSTALL_SYSTEMDUSERUNITDIR})

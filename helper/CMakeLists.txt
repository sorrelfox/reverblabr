# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The all-users virtual devices helper (ADR-0013).
#
# What it does is a library of its own, built always, so that the unit tests
# run it against a directory of their own whether KAuth is here or not.
add_library(reverblabr-helper-action STATIC
    systemdevicesaction.cpp
    systemdevicesaction.h
)
target_include_directories(reverblabr-helper-action PUBLIC ${PROJECT_SOURCE_DIR})
target_link_libraries(reverblabr-helper-action PUBLIC reverblabr-pwconf Qt6::Core)

if(NOT RB_BUILD_AUTH_HELPER)
    return()
endif()

# The helper itself, which the system bus starts as root. It links the action
# and KAuth, and nothing of PipeWire's.
add_executable(reverblabr-virtualdevices-helper main.cpp)
target_link_libraries(reverblabr-virtualdevices-helper PRIVATE reverblabr-helper-action KF6::AuthCore)
set_target_properties(reverblabr-virtualdevices-helper PROPERTIES AUTOMOC ON)

# Everything the helper needs lands in system paths D-Bus and polkit read,
# whatever the install prefix: the helper in KAuth's directory, the policy
# in polkit's, the bus policy and service file in D-Bus's (relative to the
# prefix). They are one install component, so that a development build can
# put just these in place: see docs/development.md, "Trying the all-users helper".
set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME authhelper)
install(TARGETS reverblabr-virtualdevices-helper DESTINATION ${KAUTH_HELPER_INSTALL_DIR})
kauth_install_helper_files(reverblabr-virtualdevices-helper io.github.reverblabr.virtualdevices root)
kauth_install_actions(io.github.reverblabr.virtualdevices io.github.reverblabr.virtualdevices.actions)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The version every program reports. Plain C++ with no Qt, so that
# every layer may link it.
#
# **The full version is worked out on every build**, from `git describe`,
# into one small header that is only rewritten when it changes: a new
# commit recompiles version.cpp and relinks, and nothing else.

set(RB_VERSION_FULL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/rb_version_full.h")
add_custom_target(reverblabr-version-describe
    COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DVERSION=${PROJECT_VERSION}"
            "-DOUTPUT=${RB_VERSION_FULL_HEADER}"
            -P "${PROJECT_SOURCE_DIR}/cmake/RbVersionDescribe.cmake"
    BYPRODUCTS "${RB_VERSION_FULL_HEADER}"
    VERBATIM
)

add_library(reverblabr-version STATIC buildversion.cpp buildversion.h "${RB_VERSION_FULL_HEADER}")
add_dependencies(reverblabr-version reverblabr-version-describe)
target_include_directories(reverblabr-version
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(reverblabr-version PRIVATE RB_VERSION="${PROJECT_VERSION}")
set_target_properties(reverblabr-version PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

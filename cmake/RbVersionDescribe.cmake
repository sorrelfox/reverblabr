# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Writes OUTPUT with RB_VERSION_FULL: VERSION on a release's own commit, and
# between releases VERSION+<commits since vVERSION>.g<commit>, or
# VERSION+g<commit> before that tag exists, with .dirty for a changed tree.
# Without git, VERSION alone. The file is only rewritten when what
# it says changes, so a build that changes nothing rebuilds nothing.
#
#   cmake -DSOURCE_DIR=<tree> -DVERSION=<x.y.z> -DOUTPUT=<header> -P RbVersionDescribe.cmake

set(full "${VERSION}")
find_package(Git QUIET)
if(Git_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --is-inside-work-tree
                    RESULT_VARIABLE inside OUTPUT_QUIET ERROR_QUIET)
    if(inside EQUAL 0)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" describe --tags --long
                                --match "v${VERSION}" --dirty=-dirty
                        OUTPUT_VARIABLE described RESULT_VARIABLE describeResult
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(describeResult EQUAL 0 AND described MATCHES "^v[0-9.]+-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
            set(distance "${CMAKE_MATCH_1}")
            set(commit "${CMAKE_MATCH_2}")
            set(dirty "${CMAKE_MATCH_3}")
            if(NOT distance EQUAL 0)
                set(full "${VERSION}+${distance}.g${commit}")
            endif()
            if(dirty)
                if(distance EQUAL 0)
                    set(full "${VERSION}+g${commit}.dirty")
                else()
                    string(APPEND full ".dirty")
                endif()
            endif()
        else()
            # No tag for this release yet: the commit alone says where.
            execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short HEAD
                            OUTPUT_VARIABLE commit RESULT_VARIABLE commitResult
                            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
            if(commitResult EQUAL 0)
                set(full "${VERSION}+g${commit}")
                execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" status --porcelain
                                        --untracked-files=no
                                OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
                if(changes)
                    string(APPEND full ".dirty")
                endif()
            endif()
        endif()
    endif()
endif()

set(content "// Written by cmake/RbVersionDescribe.cmake on every build; do not edit.\n#pragma once\n#define RB_VERSION_FULL \"${full}\"\n")
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" previous)
    if(previous STREQUAL content)
        return()
    endif()
endif()
file(WRITE "${OUTPUT}" "${content}")

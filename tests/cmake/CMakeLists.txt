# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Proves that the layering check in the top-level CMakeLists.txt works: each
# case configures the whole project with one deliberate violation and expects
# configuration to stop with the layering error.
foreach(case IN ITEMS rt-qt dsp-qt dsp-pipewire engine-kf)
    add_test(NAME cmake.layering.${case}
        COMMAND ${CMAKE_COMMAND}
            -S ${PROJECT_SOURCE_DIR}
            -B ${CMAKE_CURRENT_BINARY_DIR}/layering-${case}
            -G ${CMAKE_GENERATOR}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
            -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
            -DBUILD_TESTING=OFF
            -DRB_LAYERING_SELFTEST=${case}
    )
    set_tests_properties(cmake.layering.${case} PROPERTIES
        PASS_REGULAR_EXPRESSION "Layering violation: reverblabr-"
        TIMEOUT 120
    )
endforeach()

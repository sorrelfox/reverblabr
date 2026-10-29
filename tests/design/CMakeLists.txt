# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# How close the application is to the design prototype. The grabber
# is the application's half; design_compare.py renders the prototype's half
# and compares them. See README.md here.
add_executable(reverblabr-design-grab designgrab.cpp)
target_compile_definitions(reverblabr-design-grab PRIVATE
    DESIGN_GRAB_QML="${CMAKE_CURRENT_SOURCE_DIR}/DesignGrab.qml"
)
target_link_libraries(reverblabr-design-grab PRIVATE
    reverblabr-ui
    reverblabr-uiplugin
    reverblabr-styleplugin
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    KF6::I18n
    KF6::I18nQml
)

# **Not part of the default run.** It needs the local-only prototype, a
# Firefox and the network the prototype loads its scripts and fonts from,
# and takes a minute; the test presets leave the `design` label out, and
# `ctest --preset design` runs it. It fails when a state is further from the
# prototype than thresholds.tsv allows, and is skipped when it could
# not compare at all.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME design.compare
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/design_compare.py
                 --build-dir ${CMAKE_BINARY_DIR}
                 --grabber $<TARGET_FILE:reverblabr-design-grab>
                 --out ${CMAKE_BINARY_DIR}/design)
set_tests_properties(design.compare PROPERTIES
    LABELS design
    SKIP_RETURN_CODE 77
    TIMEOUT 900
)

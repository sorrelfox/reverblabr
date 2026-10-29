# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# LADSPA plugins for the tests, each library in a folder of its own
# (RB_TEST_LADSPA_DIR, RB_TEST_LADSPA_CRASH_DIR) so a test names exactly the
# plugins it wants in LADSPA_PATH. They include only the format's structures
# from the engine; they link nothing.
add_library(rb-test-ladspa MODULE testplugins.cpp)
target_include_directories(rb-test-ladspa PRIVATE ${PROJECT_SOURCE_DIR}/engine)
set_target_properties(rb-test-ladspa PROPERTIES
    PREFIX ""
    AUTOMOC OFF AUTORCC OFF
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${RB_TEST_LADSPA_DIR}"
)

add_library(rb-test-ladspa-crash MODULE crashing.cpp)
target_include_directories(rb-test-ladspa-crash PRIVATE ${PROJECT_SOURCE_DIR}/engine)
set_target_properties(rb-test-ladspa-crash PROPERTIES
    PREFIX ""
    AUTOMOC OFF AUTORCC OFF
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${RB_TEST_LADSPA_CRASH_DIR}"
)

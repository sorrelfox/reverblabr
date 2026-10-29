# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# A CLAP plugin for the tests, with a stand-in for a window that needs no
# display: a gain whose "window" moves it as a person would, through the
# host's timers and a descriptor, and closes itself on cue; its presets
# through preset discovery and preset-load. The file is in
# RB_TEST_CLAP_DIR, which a test names where CLAP plugins are looked for.
# Only the CLAP SDK's headers; it links nothing.
add_library(rb-test-clap MODULE testplugin.cpp)
target_link_libraries(rb-test-clap PRIVATE PkgConfig::CLAP)
set_target_properties(rb-test-clap PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
    AUTOMOC OFF AUTORCC OFF
    CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY "${RB_TEST_CLAP_DIR}"
)

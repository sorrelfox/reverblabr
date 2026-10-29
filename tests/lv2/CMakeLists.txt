# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# An LV2 plugin for the tests, with a window of its own that needs no
# display: a gain, a control output and two atom ports, and a UI that
# shows itself (ui:showInterface) and writes down what it is told. The
# bundle is RB_TEST_LV2_DIR, which a test names in LV2_PATH. Only LV2's
# headers; it links nothing.
set(bundle "${RB_TEST_LV2_DIR}/rb-test.lv2")
add_library(rb-test-lv2 MODULE testplugin.cpp)
add_library(rb-test-lv2-ui MODULE testui.cpp)
foreach(target rb-test-lv2 rb-test-lv2-ui)
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        AUTOMOC OFF AUTORCC OFF
        CXX_VISIBILITY_PRESET hidden
        LIBRARY_OUTPUT_DIRECTORY "${bundle}"
    )
endforeach()
configure_file(manifest.ttl "${bundle}/manifest.ttl" COPYONLY)
configure_file(rbtest.ttl "${bundle}/rbtest.ttl" COPYONLY)

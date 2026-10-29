# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

add_subdirectory(kit)   # the DSP test kit: offline runners, signals, analysis, allocation trap
# LADSPA plugins built for the tests, a folder a library, which the
# unit and PipeWire tests name in LADSPA_PATH.
set(RB_TEST_LADSPA_DIR "${CMAKE_CURRENT_BINARY_DIR}/ladspa/plugins")
set(RB_TEST_LADSPA_CRASH_DIR "${CMAKE_CURRENT_BINARY_DIR}/ladspa/crashing")
add_subdirectory(ladspa) # LADSPA plugins the tests host
# An LV2 plugin with a window that needs no display, in a bundle the tests
# name in LV2_PATH.
set(RB_TEST_LV2_DIR "${CMAKE_CURRENT_BINARY_DIR}/lv2/bundles")
add_subdirectory(lv2)   # the LV2 plugin the tests host, and its window
# A CLAP plugin with a stand-in window that needs no display, and presets,
# where the build hosts CLAP plugins.
set(RB_TEST_CLAP_DIR "${CMAKE_CURRENT_BINARY_DIR}/clap/plugins")
if(RB_BUILD_CLAP)
    add_subdirectory(clap)
endif()
add_subdirectory(unit)  # GoogleTest, one executable per engine layer
add_subdirectory(sanitizers) # checks that the sanitizers catch what they should
add_subdirectory(qml)   # Qt Quick Test for the QML user interface
add_subdirectory(pw)    # PipeWire integration tests in a private instance
add_subdirectory(bus)   # the engine on a private session bus, and the daemon behind it
add_subdirectory(ctl)   # reverblabr-ctl against the daemon, on private buses
if(RB_BUILD_PLASMOID)
    add_subdirectory(plasmoid) # the panel widget, against an engine of its own on a private bus
endif()
add_subdirectory(cmake) # build-system checks
add_subdirectory(tools) # repository tooling such as the git hooks
add_subdirectory(i18n)  # translation catalogs, string extraction and the literal-string lint
add_subdirectory(design) # the application against the design prototype; not in the default run
add_subdirectory(perf)  # benchmarks with figures to keep; not in the default run
add_subdirectory(broadcast) # a stream against a real Icecast in a container; not in the default run

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The DSP test kit: run a block or a graph offline, make signals to feed them,
# measure what came out, and catch an allocation on a real-time path.
#
# An OBJECT library rather than a static one, because allocationtrap.cpp
# replaces the global operator new and operator delete. A static library only
# contributes the object files something refers to, and nothing refers to a
# replaced operator by name, so the trap would quietly not be linked in.
add_library(reverblabr-dsp-testkit OBJECT
    allocationtrap.cpp
    allocationtrap.h
    analysis.cpp
    analysis.h
    blocksizeinvariance.cpp
    blocksizeinvariance.h
    offlinerunner.cpp
    offlinerunner.h
    signals.cpp
    signals.h
    wavfile.cpp
    wavfile.h
)
target_include_directories(reverblabr-dsp-testkit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-dsp-testkit PUBLIC reverblabr-graph reverblabr-dsp)
set_target_properties(reverblabr-dsp-testkit PROPERTIES AUTOMOC OFF AUTORCC OFF)

# libsndfile reads and writes the WAV fixtures. It is optional: without it the
# kit still does everything the generators cover, and the handful of tests
# that need a file skip themselves.
pkg_check_modules(SNDFILE IMPORTED_TARGET sndfile)
if(TARGET PkgConfig::SNDFILE)
    target_link_libraries(reverblabr-dsp-testkit PRIVATE PkgConfig::SNDFILE)
    target_compile_definitions(reverblabr-dsp-testkit PUBLIC RB_HAVE_SNDFILE=1)
else()
    target_compile_definitions(reverblabr-dsp-testkit PUBLIC RB_HAVE_SNDFILE=0)
    message(STATUS "libsndfile not found: the DSP test kit's audio-file helpers will be skipped")
endif()

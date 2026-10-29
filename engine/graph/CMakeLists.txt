# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Graph compiler and real-time runtime: plain C++23, like the DSP core.
add_library(reverblabr-graph STATIC
    compiledgraph.cpp
    compiledgraph.h
    graphdiff.cpp
    graphdiff.h
    graphspec.h
    segments.cpp
    segments.h
    topology.cpp
    topology.h
)
target_include_directories(reverblabr-graph PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-graph PUBLIC reverblabr-dsp)
set_target_properties(reverblabr-graph PROPERTIES AUTOMOC OFF AUTORCC OFF)

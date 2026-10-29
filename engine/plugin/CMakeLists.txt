# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# What an engine and a plugin host share (ADR-0014): the memory the
# parameters and the heartbeat go through, the control socket's messages,
# the host's audio cycle, and the plugins the host has built in for tests.
# Plain C++23 like the layers under it: the host and the engine's supervisor
# both link it, and all of it runs in a test with no server.
add_library(reverblabr-pluginproto STATIC
    formats.cpp
    formats.h
    hostprocessor.cpp
    hostprocessor.h
    ladspaabi.h
    messages.cpp
    messages.h
    shared.cpp
    shared.h
    testplugins.cpp
    testplugins.h
)
target_include_directories(reverblabr-pluginproto PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-pluginproto PUBLIC reverblabr-dsp reverblabr-rt)
# **Whether this build hosts CLAP plugins**: the engine looks for them,
# offers their block and starts hosts for them only then.
if(RB_BUILD_CLAP)
    target_compile_definitions(reverblabr-pluginproto PUBLIC RB_HAVE_CLAP)
endif()
# And VST3 plugins, the same way.
if(RB_BUILD_VST3)
    target_compile_definitions(reverblabr-pluginproto PUBLIC RB_HAVE_VST3)
endif()
set_target_properties(reverblabr-pluginproto PROPERTIES AUTOMOC OFF AUTORCC OFF)

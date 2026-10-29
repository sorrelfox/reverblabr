# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# reverblabr-plugin-host (ADR-0014): one plugin in a process of its own, as a
# PipeWire node of its own. No Qt: it starts with every plugin instance, and
# what it loads should be the plugin and little else.
# LV2's plugins are described and later loaded through lilv.
pkg_check_modules(LILV REQUIRED IMPORTED_TARGET lilv-0>=0.24)

# LADSPA's plugins are libraries found with dlopen() and spoken to
# through the format's own few structures, declared in the engine
# (plugin/ladspaabi.h): nothing to install to build it. A library of its own
# so a test runs a LADSPA plugin as a block, with no host and no server.
add_library(reverblabr-pluginhost-ladspa STATIC ladspaplugin.cpp ladspaplugin.h)
target_include_directories(reverblabr-pluginhost-ladspa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-pluginhost-ladspa PUBLIC reverblabr-pluginproto PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(reverblabr-pluginhost-ladspa PROPERTIES AUTOMOC OFF AUTORCC OFF)

add_executable(reverblabr-plugin-host main.cpp editor.h lv2plugin.cpp lv2plugin.h scan.cpp scan.h)
target_link_libraries(reverblabr-plugin-host PRIVATE reverblabr-version reverblabr-pluginproto reverblabr-pluginhost-ladspa
                                                     PkgConfig::PIPEWIRE PkgConfig::LILV)
# CLAP's plugins, where the build has the CLAP SDK: loaded with dlopen,
# described and run through the SDK's headers.
if(RB_BUILD_CLAP)
    target_sources(reverblabr-plugin-host PRIVATE clapplugin.cpp clapplugin.h)
    target_link_libraries(reverblabr-plugin-host PRIVATE PkgConfig::CLAP ${CMAKE_DL_LIBS})
endif()
set_target_properties(reverblabr-plugin-host PROPERTIES AUTOMOC OFF AUTORCC OFF)
install(TARGETS reverblabr-plugin-host DESTINATION ${KDE_INSTALL_LIBEXECDIR})

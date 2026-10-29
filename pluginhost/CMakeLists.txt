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
# A plugin's own window (ADR-0025): an LV2 UI is loaded through suil, and
# shown in an X11 window of the host's own, which on a Wayland desktop is
# XWayland's. CLAP plugins' windows are X11 windows too.
pkg_check_modules(SUIL REQUIRED IMPORTED_TARGET suil-0>=0.10)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)

# LADSPA's plugins are libraries found with dlopen() and spoken to
# through the format's own few structures, declared in the engine
# (plugin/ladspaabi.h): nothing to install to build it. A library of its own
# so a test runs a LADSPA plugin as a block, with no host and no server.
add_library(reverblabr-pluginhost-ladspa STATIC ladspaplugin.cpp ladspaplugin.h)
target_include_directories(reverblabr-pluginhost-ladspa PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-pluginhost-ladspa PUBLIC reverblabr-pluginproto PRIVATE ${CMAKE_DL_LIBS})
set_target_properties(reverblabr-pluginhost-ladspa PROPERTIES AUTOMOC OFF AUTORCC OFF)

# The LV2 and CLAP plugins as blocks, with their windows and presets, and
# describing them: everything of the host but its node. A library of its
# own, as LADSPA's is, so a test runs a plugin and its window as the host
# does, with no host process and no server.
add_library(reverblabr-pluginhost-formats STATIC atomring.h editor.h lv2plugin.cpp lv2plugin.h scan.cpp scan.h
                                                 x11window.cpp x11window.h)
target_include_directories(reverblabr-pluginhost-formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-pluginhost-formats PUBLIC reverblabr-pluginproto reverblabr-pluginhost-ladspa
                                                           PkgConfig::LILV PkgConfig::SUIL PkgConfig::X11)
# CLAP's plugins, where the build has the CLAP SDK: loaded with dlopen,
# described and run through the SDK's headers.
if(RB_BUILD_CLAP)
    target_sources(reverblabr-pluginhost-formats PRIVATE clapplugin.cpp clapplugin.h)
    target_link_libraries(reverblabr-pluginhost-formats PUBLIC PkgConfig::CLAP ${CMAKE_DL_LIBS})
endif()
set_target_properties(reverblabr-pluginhost-formats PROPERTIES AUTOMOC OFF AUTORCC OFF)

add_executable(reverblabr-plugin-host main.cpp)
target_link_libraries(reverblabr-plugin-host PRIVATE reverblabr-version reverblabr-pluginhost-formats PkgConfig::PIPEWIRE)
set_target_properties(reverblabr-plugin-host PROPERTIES AUTOMOC OFF AUTORCC OFF)
install(TARGETS reverblabr-plugin-host DESTINATION ${KDE_INSTALL_LIBEXECDIR})

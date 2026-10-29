# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The virtual devices' drop-in, written and read back (ADR-0013), on its own:
# the all-users helper runs as root and links this and nothing more.
# It reads the file with SPA's JSON parser, which is all in PipeWire's
# headers, so it needs their include path and not the library.
add_library(reverblabr-pwconf STATIC
    virtualdeviceconf.cpp
    virtualdeviceconf.h
)
target_include_directories(reverblabr-pwconf PUBLIC ${PROJECT_SOURCE_DIR}/engine)
# SYSTEM, as they are when the library is linked: SPA's headers are C and
# would fail our pedantic C++ warnings.
target_include_directories(reverblabr-pwconf SYSTEM
    PRIVATE $<TARGET_PROPERTY:PkgConfig::PIPEWIRE,INTERFACE_INCLUDE_DIRECTORIES>
)
target_link_libraries(reverblabr-pwconf PUBLIC Qt6::Core)

# PipeWire client: connection, registry, links and the session processing node.
add_library(reverblabr-pw STATIC
    connection.cpp
    connection.h
    feedbackguard.cpp
    feedbackguard.h
    inventory.cpp
    inventory.h
    linkmanager.cpp
    linkmanager.h
    logging.cpp
    logging.h
    pluginhostprocess.cpp
    pluginhostprocess.h
    pwobjects.cpp
    pwobjects.h
    previewstream.cpp
    previewstream.h
    registrymirror.cpp
    registrymirror.h
    sessionnode.cpp
    sessionnode.h
    sessionprocessor.cpp
    sessionprocessor.h
    version.cpp
    version.h
    virtualdevicemanager.cpp
    virtualdevicemanager.h
)
target_include_directories(reverblabr-pw PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-pw
    PUBLIC reverblabr-graph reverblabr-pwconf Qt6::Core
    PRIVATE reverblabr-pluginproto PkgConfig::PIPEWIRE
)
# Where the plugin host is (ADR-0014): installed, and in the build tree for a
# build run where it stands, unless REVERBLABR_PLUGIN_HOST says otherwise.
target_compile_definitions(reverblabr-pw PRIVATE
    RB_PLUGIN_HOST_INSTALLED="${KDE_INSTALL_FULL_LIBEXECDIR}/reverblabr-plugin-host"
    RB_PLUGIN_HOST_BUILT="$<TARGET_FILE:reverblabr-plugin-host>"
)
# The connection, the registry mirror and the inventory watcher are QObjects,
# so moc runs here.
set_target_properties(reverblabr-pw PROPERTIES AUTOMOC ON AUTORCC OFF)

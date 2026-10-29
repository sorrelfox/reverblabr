# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The engine on the session bus (ADR-0017): the daemon's side of the bridge
# (EngineService) and the window's (RemoteEngine), both driven by the facade
# and the interface XML. QtCore and QtDBus only, like the engine.
add_library(reverblabr-enginebus STATIC
    daemonreach.cpp
    daemonreach.h
    enginebus.cpp
    enginebus.h
    engineservice.cpp
    engineservice.h
    remoteengine.cpp
    remoteengine.h
)
# The XML itself, so that introspection hands out exactly what the facade
# is held to.
qt_add_resources(reverblabr-enginebus "reverblabr-engine-interface-xml"
    PREFIX "/io/github/reverblabr/bus"
    BASE "${PROJECT_SOURCE_DIR}/dbus"
    FILES "${PROJECT_SOURCE_DIR}/dbus/io.github.reverblabr.Engine1.xml"
          "${PROJECT_SOURCE_DIR}/dbus/io.github.reverblabr.Telemetry.xml"
)
target_include_directories(reverblabr-enginebus PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-enginebus PUBLIC reverblabr-engine Qt6::Core Qt6::DBus)
set_target_properties(reverblabr-enginebus PROPERTIES AUTOMOC ON AUTORCC ON)

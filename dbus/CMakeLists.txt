# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The engine's interface definition, and proof that it is one.
#
# Nothing is served on a bus in M1 ([ADR-0003]), but the XML is still put
# through qdbusxml2cpp and the client proxy it produces is compiled. That is
# what says the interface really is a D-Bus interface: an argument of a type
# D-Bus cannot carry, or a shape it cannot express, fails here rather than on
# the day the engine moves into a daemon.
set(RB_ENGINE_INTERFACE_XML ${CMAKE_CURRENT_SOURCE_DIR}/io.github.reverblabr.Engine1.xml
    CACHE INTERNAL "The engine's D-Bus interface definition")

qt_add_dbus_interface(RB_ENGINE_INTERFACE_SOURCES ${RB_ENGINE_INTERFACE_XML} engineinterface)

add_library(reverblabr-engine-interface STATIC ${RB_ENGINE_INTERFACE_SOURCES})
target_link_libraries(reverblabr-engine-interface PRIVATE Qt6::DBus)
target_include_directories(reverblabr-engine-interface PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
set_target_properties(reverblabr-engine-interface PROPERTIES AUTOMOC ON AUTORCC OFF)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The panel widget (ADR-0024): its package as Plasma installs it, and its
# list and representations against an engine of the test's own.

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# **The package as Plasma reads it**: the metadata names the widget by the
# folder it is installed in and says the version VERSION says, every file
# the QML needs is there, and kpackagetool6, where it is installed, takes
# it into a folder of the test's own.
find_program(RB_KPACKAGETOOL kpackagetool6)
add_test(NAME plasmoid.package
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_package.py
                 ${RB_PLASMOID_PACKAGE} ${PROJECT_SOURCE_DIR}/plasmoid/package ${RB_PLASMOID_ID} ${PROJECT_SOURCE_DIR}/VERSION "${RB_KPACKAGETOOL}")

# **The list and the representations on a bus of their own.** The bus
# has no activation files at all, so nothing on it can start an engine:
# the engine is the test's fake, which takes the name when a test says so,
# and a call that would have started a real one fails instead, which is
# how a test sees that the widget never asks for that by itself.
find_program(RB_DBUS_RUN_SESSION dbus-run-session REQUIRED)
set(RB_PLASMOID_BUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/private-bus")
file(MAKE_DIRECTORY "${RB_PLASMOID_BUS_DIR}/services")
file(WRITE "${RB_PLASMOID_BUS_DIR}/session.conf"
"<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"
 \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <servicedir>${RB_PLASMOID_BUS_DIR}/services</servicedir>
  <policy context=\"default\">
    <allow send_destination=\"*\" eavesdrop=\"true\"/>
    <allow eavesdrop=\"true\"/>
    <allow own=\"*\"/>
  </policy>
</busconfig>
")

add_executable(reverblabr-plasmoid-test tst_plasmoid.cpp)
target_compile_definitions(reverblabr-plasmoid-test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    RB_QML_IMPORT_DIR="${QT_QML_OUTPUT_DIRECTORY}"
    RB_PLASMOID_DOMAIN="plasma_applet_${RB_PLASMOID_ID}"
)
target_link_libraries(reverblabr-plasmoid-test PRIVATE
    reverblabr-telemetry
    Qt6::DBus
    Qt6::Qml
    Qt6::QuickTest
    KF6::I18nQml
)
set_target_properties(reverblabr-plasmoid-test PROPERTIES AUTOMOC ON)
add_dependencies(reverblabr-plasmoid-test reverblabr-plasmoidplugin)

set(RB_PLASMOID_TEST_HOME "${CMAKE_CURRENT_BINARY_DIR}/test-home")
file(MAKE_DIRECTORY "${RB_PLASMOID_TEST_HOME}")
set(_rb_plasmoid_environment
    "QT_QPA_PLATFORM=offscreen;LC_ALL=en_US.UTF-8;HOME=${RB_PLASMOID_TEST_HOME};XDG_DATA_HOME=${RB_PLASMOID_TEST_HOME}/data;XDG_CONFIG_HOME=${RB_PLASMOID_TEST_HOME}/config;XDG_STATE_HOME=${RB_PLASMOID_TEST_HOME}/state;XDG_CACHE_HOME=${RB_PLASMOID_TEST_HOME}/cache")

add_test(NAME plasmoid.list
         COMMAND ${RB_DBUS_RUN_SESSION} --config-file=${RB_PLASMOID_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-plasmoid-test> -input ${CMAKE_CURRENT_SOURCE_DIR}/tst_sessionlist.qml)
set_tests_properties(plasmoid.list PROPERTIES TIMEOUT 180 ENVIRONMENT "${_rb_plasmoid_environment}")

# **The representations are drawn with Plasma's own controls**, which only
# a Plasma desktop (libplasma) installs. Without them the list's tests
# still run, and this one is not there, which configuring says.
find_path(RB_PLASMA_COMPONENTS_DIR org/kde/plasma/components/qmldir
          PATHS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}" "${KDE_INSTALL_FULL_QMLDIR}"
          NO_DEFAULT_PATH)
if(RB_PLASMA_COMPONENTS_DIR)
    add_test(NAME plasmoid.representations
             COMMAND ${RB_DBUS_RUN_SESSION} --config-file=${RB_PLASMOID_BUS_DIR}/session.conf
                     -- $<TARGET_FILE:reverblabr-plasmoid-test> -input ${CMAKE_CURRENT_SOURCE_DIR}/tst_representations.qml)
    set_tests_properties(plasmoid.representations PROPERTIES TIMEOUT 180 ENVIRONMENT "${_rb_plasmoid_environment}")
else()
    message(STATUS "plasmoid.representations is left out: Plasma's QML controls (libplasma) are not installed")
endif()

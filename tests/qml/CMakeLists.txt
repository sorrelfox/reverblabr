# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Runs every tst_*.qml file in this directory against the real QML module.
# With a block for one ear of the tests' own, from the unit tests.
add_executable(reverblabr-qml-test tst_qml.cpp ../unit/dsp/earblock.cpp ../unit/dsp/earblock.h)
target_compile_definitions(reverblabr-qml-test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(reverblabr-qml-test PRIVATE
    reverblabr-ui
    reverblabr-uiplugin
    reverblabr-styleplugin
    Qt6::QuickTest
    Qt6::Qml
    KF6::I18n
    KF6::I18nQml
)

add_test(NAME qml.mainwindow COMMAND reverblabr-qml-test)
# The harness already hands the window its own config file; the XDG entries
# are for anything that asks QStandardPaths directly, so that no test can
# write into the home directory of whoever runs it.
set(RB_QML_TEST_HOME "${CMAKE_CURRENT_BINARY_DIR}/test-home")
file(MAKE_DIRECTORY "${RB_QML_TEST_HOME}")
set_tests_properties(qml.mainwindow PROPERTIES
    ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;XDG_DATA_HOME=${RB_QML_TEST_HOME}/data;XDG_CONFIG_HOME=${RB_QML_TEST_HOME}/config;XDG_STATE_HOME=${RB_QML_TEST_HOME}/state;XDG_CACHE_HOME=${RB_QML_TEST_HOME}/cache"
)

# **The same suite under the Reverblabr control style** ([ADR-0012]).
# The window is drawn by one of two styles, and a control that works in only
# one of them is a bug: Basic stands in for the desktop style above (which
# needs a display to draw), and this is the application's own. A home of its
# own, so the two can run at once.
#
# **On a light desktop**: the run is in the application's default,
# the design's dark look, and the desktop it sits on is Breeze Light, read
# through KDE's platform theme where the system has one. That is the case
# the style has to survive — the platform handing a menu, a tooltip or a
# label the desktop's light palette and font — and the one in which a menu
# drawn with them opened light in a dark window. Without the platform theme
# or the scheme the run still runs, on Qt's own defaults.
add_test(NAME qml.mainwindow.reverblabr-style COMMAND reverblabr-qml-test)
set(RB_QML_STYLE_TEST_HOME "${CMAKE_CURRENT_BINARY_DIR}/test-home-reverblabr-style")
file(MAKE_DIRECTORY "${RB_QML_STYLE_TEST_HOME}/config")
if(EXISTS "/usr/share/color-schemes/BreezeLight.colors")
    file(READ "/usr/share/color-schemes/BreezeLight.colors" RB_LIGHT_SCHEME)
    file(WRITE "${RB_QML_STYLE_TEST_HOME}/config/kdeglobals" "${RB_LIGHT_SCHEME}")
endif()
set_tests_properties(qml.mainwindow.reverblabr-style PROPERTIES
    ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=kde;QT_QUICK_CONTROLS_STYLE=Reverblabr;XDG_DATA_HOME=${RB_QML_STYLE_TEST_HOME}/data;XDG_CONFIG_HOME=${RB_QML_STYLE_TEST_HOME}/config;XDG_STATE_HOME=${RB_QML_STYLE_TEST_HOME}/state;XDG_CACHE_HOME=${RB_QML_STYLE_TEST_HOME}/cache"
)

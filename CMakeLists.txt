# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

cmake_minimum_required(VERSION 3.24)

# **The version is written once, in VERSION**: everything built takes
# it from here, and what cannot be built from it (the AppStream release
# notes, a release tag) is checked against it. tools/release.sh changes it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" RB_VERSION LIMIT_COUNT 1)
if(NOT RB_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "VERSION must hold MAJOR.MINOR.PATCH on its first line, not \"${RB_VERSION}\"")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")

project(reverblabr VERSION ${RB_VERSION} LANGUAGES CXX)

# Minimum versions: the oldest supported platform (Kubuntu 26.04) sets them.
# Keep in sync with tools/check-deps.sh and docs/development.md.
set(QT_MIN_VERSION "6.10.0")
set(KF_MIN_VERSION "6.24.0")
set(PIPEWIRE_MIN_VERSION "1.6.0")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${PROJECT_SOURCE_DIR}/cmake)

# GCC's -Wmissing-include-dirs fires on the include directories Qt's code
# generators create during the build, which breaks builds with -Werror.
set(KDE_SKIP_MISSING_INCLUDE_DIRS_WARNINGS_SETTINGS TRUE)

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMDeprecationSettings)
include(ECMQmlModule)
include(ECMSetupVersion)
include(ECMInstallIcons)
include(RbTranslateDataFile)
include(FeatureSummary)

# Our own code reports errors as values, but the standard library can throw,
# so exceptions stay enabled. See docs/conventions.md.
kde_enable_exceptions()

# With Clang, check functions marked RB_NONBLOCKING at compile time: they must
# not allocate, lock or call functions that might (see engine/rt/realtime.h).
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wfunction-effects)
endif()

# A target sees only the include directories it declares, not those of every
# directory it links against.
set(CMAKE_INCLUDE_CURRENT_DIR_IN_INTERFACE OFF)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core DBus Gui Widgets Qml Quick QuickControls2 Sql Svg Xml)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Config Kirigami I18n CoreAddons DBusAddons WindowSystem KIO GuiAddons Notifications StatusNotifierItem GlobalAccel)
# **The compositor's own window menu** for the frameless window of the
# Reverblabr appearances: Qt has no public call for it, and its Wayland
# client internals do. Optional — without them a right-click on the header
# opens a smaller menu of our own.
find_package(Qt6 ${QT_MIN_VERSION} QUIET COMPONENTS WaylandClientPrivate)
# **The all-users virtual devices**: a small helper the system bus
# starts as root, asked through KAuth and polkit. Optional — without KAuth
# the app still builds, and a device for all users is refused as needing a
# helper that is not there.
find_package(KF6Auth ${KF_MIN_VERSION} QUIET CONFIG)
set_package_properties(KF6Auth PROPERTIES
    TYPE OPTIONAL
    PURPOSE "Virtual devices for all users of the computer, through an administrator's password"
)
option(RB_BUILD_AUTH_HELPER "Build the all-users virtual devices helper and let the app call it (needs KF6Auth)" ${KF6Auth_FOUND})
if(RB_BUILD_AUTH_HELPER AND NOT KF6Auth_FOUND)
    message(FATAL_ERROR "RB_BUILD_AUTH_HELPER is on, but KF6Auth was not found (kf6-kauth-devel / libkf6auth-dev)")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3>=${PIPEWIRE_MIN_VERSION})
# **Speech Denoise** (ADR-0016): RNNoise, the distribution's. Optional —
# Fedora packages it (rnnoise-devel) but Kubuntu 26.04 does not, and a build
# without it has every other block and no Speech Denoise in the library.
pkg_check_modules(RNNOISE QUIET IMPORTED_TARGET rnnoise>=0.2)
option(RB_BUILD_SPEECH_DENOISE "Build the Speech Denoise block (needs RNNoise 0.2)" ${RNNOISE_FOUND})
if(RB_BUILD_SPEECH_DENOISE AND NOT RNNOISE_FOUND)
    message(FATAL_ERROR "RB_BUILD_SPEECH_DENOISE is on, but RNNoise 0.2 was not found (rnnoise-devel on Fedora)")
endif()
add_feature_info(SpeechDenoise RB_BUILD_SPEECH_DENOISE "the Speech Denoise block, through RNNoise (rnnoise-devel on Fedora; not packaged for Kubuntu 26.04)")
if(NOT RNNOISE_FOUND)
    message(STATUS "Speech Denoise will not be built: RNNoise 0.2 was not found. Every other block is; see ADR-0016.")
elseif(NOT RB_BUILD_SPEECH_DENOISE)
    message(STATUS "Speech Denoise will not be built: RB_BUILD_SPEECH_DENOISE is off.")
endif()
# **CLAP plugins**: the CLAP SDK's headers, the distribution's. Optional
# for the same reason as RNNoise — Fedora packages them (clap-devel) and
# Kubuntu 26.04 does not — and a build without them has no CLAP block and
# does not look for CLAP plugins.
pkg_check_modules(CLAP QUIET IMPORTED_TARGET clap>=1.2)
option(RB_BUILD_CLAP "Host CLAP plugins (needs the CLAP SDK 1.2)" ${CLAP_FOUND})
if(RB_BUILD_CLAP AND NOT CLAP_FOUND)
    message(FATAL_ERROR "RB_BUILD_CLAP is on, but the CLAP SDK 1.2 was not found (clap-devel on Fedora)")
endif()
add_feature_info(ClapPlugins RB_BUILD_CLAP "CLAP plugins in the plugin host (clap-devel on Fedora; not packaged for Kubuntu 26.04)")
if(NOT CLAP_FOUND)
    message(STATUS "CLAP plugins will not be hosted: the CLAP SDK 1.2 was not found. LV2 plugins are.")
elseif(NOT RB_BUILD_CLAP)
    message(STATUS "CLAP plugins will not be hosted: RB_BUILD_CLAP is off.")
endif()

if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test QuickTest)
    find_package(GTest REQUIRED)
endif()

qt_policy(SET QTP0001 NEW)

# Fail the build on API that the minimum versions already deprecate.
ecm_set_disabled_deprecation_versions(QT ${QT_MIN_VERSION} KF ${KF_MIN_VERSION})

add_subdirectory(version)
add_subdirectory(dbus)
add_subdirectory(engine)
add_subdirectory(helper)
add_subdirectory(pluginhost)
add_subdirectory(notify)
add_subdirectory(daemon)
add_subdirectory(ctl)
add_subdirectory(app)
# **The panel widget** (ADR-0024). Nothing of Plasma is needed to build it,
# only to show it, so it is built by default; a build for a desktop that is
# not Plasma can leave it out.
option(RB_BUILD_PLASMOID "Build the Plasma panel widget, Reverblabr Sessions" ON)
add_feature_info(PlasmaWidget RB_BUILD_PLASMOID "the Plasma panel widget, Reverblabr Sessions")
if(RB_BUILD_PLASMOID)
    add_subdirectory(plasmoid)
endif()

# Translations: every po/<language>/reverblabr.po is compiled and installed, so
# adding a language needs no code change. The template is regenerated with
# the extract-messages target and is not committed.
ki18n_install(po)
add_custom_target(extract-messages
    COMMAND ${PROJECT_SOURCE_DIR}/tools/extract-messages.sh ${PROJECT_SOURCE_DIR}/po
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    COMMENT "Extracting translatable strings into po/reverblabr.pot"
    VERBATIM
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Test hook: tests/cmake configures the project with one of these values to
# prove that the layering check below catches the violation.
set(RB_LAYERING_SELFTEST "" CACHE STRING "Test only: inject a layering violation (rt-qt, dsp-qt, dsp-pipewire, engine-kf)")
mark_as_advanced(RB_LAYERING_SELFTEST)
if(RB_LAYERING_SELFTEST STREQUAL "rt-qt")
    target_link_libraries(reverblabr-rt PRIVATE Qt6::Core)
elseif(RB_LAYERING_SELFTEST STREQUAL "dsp-qt")
    target_link_libraries(reverblabr-dsp PRIVATE Qt6::Core)
elseif(RB_LAYERING_SELFTEST STREQUAL "dsp-pipewire")
    target_link_libraries(reverblabr-dsp PRIVATE PkgConfig::PIPEWIRE)
elseif(RB_LAYERING_SELFTEST STREQUAL "engine-kf")
    target_link_libraries(reverblabr-engine PRIVATE KF6::CoreAddons)
elseif(RB_LAYERING_SELFTEST)
    message(FATAL_ERROR "Unknown RB_LAYERING_SELFTEST value: ${RB_LAYERING_SELFTEST}")
endif()

# The layer rules; see docs/architecture.md.
include(RbLayering)
rb_check_layering(
    TARGETS reverblabr-rt reverblabr-dsp reverblabr-blocks reverblabr-graph reverblabr-record reverblabr-pluginproto
    FORBID qt kf pipewire
    REASON "The real-time, DSP, block, graph and recording layers are plain C++23, so they can run on the real-time thread (or beside it) and be tested offline."
)
rb_check_layering(
    TARGETS reverblabr-telemetry reverblabr-pwconf reverblabr-pw reverblabr-engine reverblabr-enginebus reverblabr-daemon-core reverblabrd
    FORBID kf qt-gui
    REASON "The engine uses only QtCore, QtDBus, QtSql and PipeWire, so it can later run as a headless daemon."
)
rb_check_layering(
    TARGETS reverblabr-notify
    FORBID kf qt-gui pipewire
    REASON "What is worth a notification, and its words, is shared with the daemon, which has no KDE Frameworks and no GUI (ADR-0017)."
)
rb_check_layering(
    TARGETS reverblabr-ctl reverblabr-ctl-core
    FORBID kf qt-gui pipewire
    REASON "reverblabr-ctl is a client of the public D-Bus API: QtCore and QtDBus, so it runs from a terminal, a script or cron, with nothing of the engine or the window in it."
)
if(RB_BUILD_PLASMOID)
    rb_check_layering(
        TARGETS reverblabr-plasmoidplugin
        FORBID kf pipewire
        REASON "The panel widget's plugin runs inside the desktop shell: it talks to the engine over the bus and reads the levels from the engine's memory, with QtCore, QtDBus and QtQml, and takes in nothing of the engine or the window."
    )
endif()
rb_check_layering(
    TARGETS reverblabr-plugin-host
    FORBID qt kf
    REASON "A plugin host starts with every plugin instance: the plugin, PipeWire and the protocol, and nothing else."
)
rb_check_layering(
    TARGETS reverblabr-helper-action
    FORBID kf qt-gui pipewire
    REASON "The all-users helper runs as root: it reads its arguments and writes one file with QtCore and the drop-in code, and loads nothing else."
)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)

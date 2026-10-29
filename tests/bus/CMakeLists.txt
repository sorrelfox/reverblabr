# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The engine on the session bus and the daemon behind it (ADR-0017).
#
# **A bus of their own.** The test runs inside dbus-run-session with the
# configuration below, whose only service directory is one here, holding an
# activation file for the reverblabrd just built. So a call starts that
# daemon and no other, nothing is installed or enabled for systemd, and the
# real session bus never sees any of it.

find_program(RB_DBUS_RUN_SESSION dbus-run-session REQUIRED)

# A window, as a process of its own that can be killed.
add_executable(reverblabr-bus-client client.cpp)
target_link_libraries(reverblabr-bus-client PRIVATE reverblabr-enginebus Qt6::Core)

add_executable(reverblabr-bus-test recordingfacade.h test_enginebus.cpp)
target_link_libraries(reverblabr-bus-test PRIVATE reverblabr-enginebus Qt6::DBus Qt6::Test GTest::gtest)
set_target_properties(reverblabr-bus-test PROPERTIES AUTOMOC ON)
target_compile_definitions(reverblabr-bus-test PRIVATE RB_DAEMON="$<TARGET_FILE:reverblabrd>"
                                                       RB_BUS_CLIENT="$<TARGET_FILE:reverblabr-bus-client>"
                                                       RB_ENGINE_API_PIN="${PROJECT_SOURCE_DIR}/dbus/io.github.reverblabr.Engine1.api")
add_dependencies(reverblabr-bus-test reverblabrd reverblabr-bus-client)

set(RB_BUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/private-bus")
# **Without SystemdService=**: on this bus the daemon starts itself, and no
# systemd unit is looked for, let alone started.
file(GENERATE OUTPUT "${RB_BUS_DIR}/services/io.github.reverblabr.Engine.service" CONTENT
"[D-BUS Service]
Name=io.github.reverblabr.Engine
Exec=$<TARGET_FILE:reverblabrd>
")
file(WRITE "${RB_BUS_DIR}/session.conf"
"<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"
 \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <servicedir>${RB_BUS_DIR}/services</servicedir>
  <policy context=\"default\">
    <allow send_destination=\"*\" eavesdrop=\"true\"/>
    <allow eavesdrop=\"true\"/>
    <allow own=\"*\"/>
  </policy>
</busconfig>
")

# A home in the build tree for anything that is not given one of its own
# (each test gives its daemons a temporary one), and a PipeWire socket that
# is not there, so no engine here can touch the real audio.
set(RB_BUS_HOME "${CMAKE_CURRENT_BINARY_DIR}/home")
file(MAKE_DIRECTORY "${RB_BUS_HOME}/data" "${RB_BUS_HOME}/config" "${RB_BUS_HOME}/state" "${RB_BUS_HOME}/cache")
# **A runtime directory of their own**, as a login has one: a daemon notes
# there that Auto Run has run (ADR-0023), and one started by a test must
# never say so in the desktop's. Only its owner may enter it, as Qt asks.
file(MAKE_DIRECTORY "${RB_BUS_HOME}/runtime")
file(CHMOD "${RB_BUS_HOME}/runtime" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
# **A PID namespace of its own, where there is one to be had.** The test
# kills daemons by pid, and a pid gone wrong (0, -1) signals the process
# group or every process the user owns: the desktop session. Inside the
# namespace the test is pid 1 and sees only what it started, so such a kill
# reaches nothing else. Where unprivileged namespaces are refused (Ubuntu's
# AppArmor default), the test's own guard on each pid is what is left.
set(RB_BUS_SANDBOX)
find_program(RB_UNSHARE unshare)
if(RB_UNSHARE)
    set(_rb_unshare_args --user --map-current-user --pid --fork --kill-child --mount-proc)
    execute_process(COMMAND ${RB_UNSHARE} ${_rb_unshare_args} true
                    RESULT_VARIABLE _rb_unshare_result OUTPUT_QUIET ERROR_QUIET)
    if(_rb_unshare_result EQUAL 0)
        set(RB_BUS_SANDBOX ${RB_UNSHARE} ${_rb_unshare_args})
    endif()
endif()
if(NOT RB_BUS_SANDBOX)
    message(STATUS "bus.engine runs without a PID namespace of its own; unshare cannot make one here")
endif()

add_test(NAME bus.engine
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-bus-test>)
set_tests_properties(bus.engine PROPERTIES
    TIMEOUT 180
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime;PIPEWIRE_REMOTE=rb-test-no-such-socket"
)

# Which engine a window gets: the same binary on four buses, each of
# its own and each in a PID namespace of its own, as bus.engine is.
# RB_REACH_BUS names the situation; the tests about the others skip.
find_program(RB_FALSE false REQUIRED)
add_executable(reverblabr-reach-test test_daemonreach.cpp)
target_link_libraries(reverblabr-reach-test PRIVATE reverblabr-enginebus Qt6::DBus GTest::gtest)
add_dependencies(reverblabr-reach-test reverblabrd)

# A bus with no activation files, and one whose only file starts a program
# that exits at once.
set(RB_REACH_DIR "${CMAKE_CURRENT_BINARY_DIR}/reach")
file(MAKE_DIRECTORY "${RB_REACH_DIR}/empty-services")
file(WRITE "${RB_REACH_DIR}/refused-services/io.github.reverblabr.Engine.service"
"[D-BUS Service]
Name=io.github.reverblabr.Engine
Exec=${RB_FALSE}
")
foreach(_rb_reach empty refused)
    file(WRITE "${RB_REACH_DIR}/${_rb_reach}.conf"
"<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"
 \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <servicedir>${RB_REACH_DIR}/${_rb_reach}-services</servicedir>
  <policy context=\"default\">
    <allow send_destination=\"*\" eavesdrop=\"true\"/>
    <allow eavesdrop=\"true\"/>
    <allow own=\"*\"/>
  </policy>
</busconfig>
")
endforeach()

set(_rb_reach_environment
    "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime;PIPEWIRE_REMOTE=rb-test-no-such-socket")
# **No bus at all**: an address with nothing behind it, and no
# dbus-run-session.
add_test(NAME bus.reach.nobus COMMAND ${RB_BUS_SANDBOX} $<TARGET_FILE:reverblabr-reach-test>)
set_tests_properties(bus.reach.nobus PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "${_rb_reach_environment};RB_REACH_BUS=nobus;DBUS_SESSION_BUS_ADDRESS=unix:path=${RB_REACH_DIR}/no-such-bus")
foreach(_rb_reach empty refused)
    add_test(NAME bus.reach.${_rb_reach}
             COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_REACH_DIR}/${_rb_reach}.conf
                     -- $<TARGET_FILE:reverblabr-reach-test>)
    set_tests_properties(bus.reach.${_rb_reach} PROPERTIES
        TIMEOUT 60
        ENVIRONMENT "${_rb_reach_environment};RB_REACH_BUS=${_rb_reach}")
endforeach()
# **The daemon just built**, started by the bus bus.engine uses.
add_test(NAME bus.reach.activated
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-reach-test>)
set_tests_properties(bus.reach.activated PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "${_rb_reach_environment};RB_REACH_BUS=activated")

# **Desktop notifications against a notification server of the test's own**:
# the window's through KNotifications, the daemon's straight to the
# server, and the daemon keeping quiet while a window says them. On the same
# private bus, in the same PID namespace, as bus.engine: the test owns
# org.freedesktop.Notifications there, which it must never do on a desktop.
add_executable(reverblabr-notify-bus-test test_notifications.cpp)
target_link_libraries(reverblabr-notify-bus-test PRIVATE
    reverblabr-ui reverblabr-enginebus reverblabr-notify KF6::Notifications Qt6::DBus Qt6::Gui Qt6::Test GTest::gtest)
target_compile_definitions(reverblabr-notify-bus-test PRIVATE RB_DAEMON="$<TARGET_FILE:reverblabrd>")
add_dependencies(reverblabr-notify-bus-test reverblabrd)
# KNotifications finds reverblabr.notifyrc under the data paths, as it does
# once Reverblabr is installed.
set(RB_BUS_NOTIFY_DATA "${CMAKE_CURRENT_BINARY_DIR}/notify-data")
file(MAKE_DIRECTORY "${RB_BUS_NOTIFY_DATA}/knotifications6")
configure_file("${PROJECT_SOURCE_DIR}/notify/reverblabr.notifyrc" "${RB_BUS_NOTIFY_DATA}/knotifications6/reverblabr.notifyrc" COPYONLY)
add_test(NAME bus.notifications
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-notify-bus-test>)
set_tests_properties(bus.notifications PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime;XDG_DATA_DIRS=${RB_BUS_NOTIFY_DATA}:/usr/local/share:/usr/share;QT_QPA_PLATFORM=offscreen;PIPEWIRE_REMOTE=rb-test-no-such-socket"
)


# **Reverblabr's icon in the system tray**: the real KStatusNotifierItem
# against a StatusNotifierWatcher the test starts, in a process of its own,
# on the same private bus and in the same PID namespace as bus.engine. On a
# desktop's bus it would put an icon in that desktop's tray, which both the
# test and the watcher refuse to do.
add_executable(reverblabr-fake-sni-watcher fakesniwatcher.cpp)
target_link_libraries(reverblabr-fake-sni-watcher PRIVATE Qt6::DBus)
add_executable(reverblabr-tray-bus-test test_tray.cpp)
target_link_libraries(reverblabr-tray-bus-test PRIVATE
    reverblabr-ui KF6::StatusNotifierItem Qt6::Widgets Qt6::DBus Qt6::Test GTest::gtest)
target_compile_definitions(reverblabr-tray-bus-test PRIVATE
    RB_FAKE_WATCHER="$<TARGET_FILE:reverblabr-fake-sni-watcher>"
    RB_DESKTOP_FILE="${PROJECT_SOURCE_DIR}/app/io.github.reverblabr.Reverblabr.desktop"
    RB_APP_ICON_SVG="${PROJECT_SOURCE_DIR}/app/appicon/sc-apps-io.github.reverblabr.Reverblabr.svg")
add_dependencies(reverblabr-tray-bus-test reverblabr-fake-sni-watcher)
add_test(NAME bus.tray
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-tray-bus-test>)
set_tests_properties(bus.tray PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime;QT_QPA_PLATFORM=offscreen;PIPEWIRE_REMOTE=rb-test-no-such-socket"
)


# **Reverblabr's global shortcuts**: the real KGlobalAccel against a
# kglobalaccel the test starts, in a process of its own, on the same private
# bus and in the same PID namespace as bus.engine. On a desktop's bus it would
# put Reverblabr's actions in that desktop's System Settings, which both the
# test and the fake daemon refuse to do.
add_executable(reverblabr-fake-kglobalaccel fakekglobalaccel.cpp)
target_link_libraries(reverblabr-fake-kglobalaccel PRIVATE Qt6::DBus)
add_executable(reverblabr-shortcuts-bus-test test_shortcuts.cpp)
target_link_libraries(reverblabr-shortcuts-bus-test PRIVATE
    reverblabr-ui KF6::GlobalAccel Qt6::Widgets Qt6::DBus Qt6::Test GTest::gtest)
target_compile_definitions(reverblabr-shortcuts-bus-test PRIVATE
    RB_FAKE_KGLOBALACCEL="$<TARGET_FILE:reverblabr-fake-kglobalaccel>")
add_dependencies(reverblabr-shortcuts-bus-test reverblabr-fake-kglobalaccel)
add_test(NAME bus.shortcuts
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-shortcuts-bus-test>)
set_tests_properties(bus.shortcuts PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime;QT_QPA_PLATFORM=offscreen;PIPEWIRE_REMOTE=rb-test-no-such-socket"
)

# Auto Run (ADR-0021): the sessions that start with the engine, and the
# engine's user unit enabled while there are any — against a service manager
# the test puts on its own private bus, never the desktop's.
add_executable(reverblabr-autorun-bus-test test_autorun.cpp)
target_link_libraries(reverblabr-autorun-bus-test PRIVATE
    reverblabr-daemon-core reverblabr-engine reverblabr-blocks Qt6::DBus Qt6::Test GTest::gtest)
set_target_properties(reverblabr-autorun-bus-test PROPERTIES AUTOMOC ON)
add_test(NAME bus.autorun
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-autorun-bus-test>)
set_tests_properties(bus.autorun PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime;PIPEWIRE_REMOTE=rb-test-no-such-socket"
)


# The sessions' timers as systemd user units: written, enabled, restarted,
# stopped and removed as the timers change, against a service manager on
# the private bus and into a unit directory of the test's own, never the
# desktop's.
add_executable(reverblabr-timers-bus-test test_timerschedule.cpp)
target_link_libraries(reverblabr-timers-bus-test PRIVATE
    reverblabr-daemon-core reverblabr-engine reverblabr-blocks Qt6::DBus Qt6::Test GTest::gtest)
set_target_properties(reverblabr-timers-bus-test PROPERTIES AUTOMOC ON)
add_test(NAME bus.timers
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-timers-bus-test>)
set_tests_properties(bus.timers PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;PIPEWIRE_REMOTE=rb-test-no-such-socket"
)

# **One Reverblabr a desktop**: the real application, launched twice,
# on the same private bus and in the same PID namespace as bus.engine. A
# second launch hands its arguments to the first, as the desktop entry's New
# Session and All Sessions do. On a desktop's bus the second launch would
# reach the Reverblabr the person has open, which the test refuses to do.
add_executable(reverblabr-launch-bus-test test_launch.cpp)
target_link_libraries(reverblabr-launch-bus-test PRIVATE Qt6::DBus GTest::gtest)
target_compile_definitions(reverblabr-launch-bus-test PRIVATE RB_APP="$<TARGET_FILE:reverblabr>")
add_dependencies(reverblabr-launch-bus-test reverblabr)
add_test(NAME bus.launch
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-launch-bus-test>)
set_tests_properties(bus.launch PROPERTIES
    TIMEOUT 400
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime;QT_QPA_PLATFORM=offscreen;PIPEWIRE_REMOTE=rb-test-no-such-socket"
)

# **The sessions in KRunner** (ADR-0023): the runner the daemon serves,
# called as KRunner calls it, on the same private bus and in the same PID
# namespace as bus.engine. On a desktop's bus a query would reach the
# engine the person is using. The locale is pinned: the test reads the
# runner's words.
add_executable(reverblabr-krunner-bus-test test_krunner.cpp)
target_link_libraries(reverblabr-krunner-bus-test PRIVATE
    reverblabr-daemon-core reverblabr-enginebus reverblabr-engine Qt6::DBus GTest::gtest)
target_compile_definitions(reverblabr-krunner-bus-test PRIVATE RB_DAEMON="$<TARGET_FILE:reverblabrd>")
add_dependencies(reverblabr-krunner-bus-test reverblabrd)
add_test(NAME bus.krunner
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-krunner-bus-test>)
set_tests_properties(bus.krunner PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime;PIPEWIRE_REMOTE=rb-test-no-such-socket;LC_ALL=en_US.UTF-8;LANGUAGE=en_US"
)

# **The media players on the session bus** (MPRIS), as a Broadcast block's
# title follows them: the watcher and a stream's feed, against players the
# test starts, each a process of its own, on the same private bus and in
# the same PID namespace as bus.engine. On a desktop's bus they would read
# the person's own players, which both the test and the fake player refuse.
add_executable(reverblabr-fake-mpris-player fakemprisplayer.cpp)
target_link_libraries(reverblabr-fake-mpris-player PRIVATE Qt6::DBus)
add_executable(reverblabr-mpris-bus-test test_mpris.cpp)
target_link_libraries(reverblabr-mpris-bus-test PRIVATE reverblabr-mpris Qt6::DBus Qt6::Test GTest::gtest)
target_compile_definitions(reverblabr-mpris-bus-test PRIVATE
    RB_FAKE_MPRIS_PLAYER="$<TARGET_FILE:reverblabr-fake-mpris-player>")
add_dependencies(reverblabr-mpris-bus-test reverblabr-fake-mpris-player)
add_test(NAME bus.mpris
         COMMAND ${RB_BUS_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_BUS_DIR}/session.conf
                 -- $<TARGET_FILE:reverblabr-mpris-bus-test>)
set_tests_properties(bus.mpris PROPERTIES
    TIMEOUT 120
    ENVIRONMENT "HOME=${RB_BUS_HOME};XDG_DATA_HOME=${RB_BUS_HOME}/data;XDG_CONFIG_HOME=${RB_BUS_HOME}/config;XDG_STATE_HOME=${RB_BUS_HOME}/state;XDG_CACHE_HOME=${RB_BUS_HOME}/cache;XDG_RUNTIME_DIR=${RB_BUS_HOME}/runtime"
)

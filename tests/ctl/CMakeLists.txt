# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# reverblabr-ctl against reverblabrd, on buses of their own, as
# bus.engine is (tests/bus): dbus-run-session with a configuration whose only
# service is the reverblabrd just built, inside a PID namespace of its own,
# with a home in the build tree and a PipeWire socket that is not there.
# Nothing is installed, and the real session bus never sees any of it.

find_program(RB_DBUS_RUN_SESSION dbus-run-session REQUIRED)
find_package(Gettext REQUIRED)

# The test catalog: the tool read in a language that does not exist.
set(RB_CTL_TEST_LOCALEDIR ${CMAKE_CURRENT_BINARY_DIR}/locale)
set(_rb_ctl_catalog ${RB_CTL_TEST_LOCALEDIR}/xx/LC_MESSAGES/reverblabr-ctl.mo)
add_custom_command(
    OUTPUT ${_rb_ctl_catalog}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RB_CTL_TEST_LOCALEDIR}/xx/LC_MESSAGES
    COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check -o ${_rb_ctl_catalog} ${CMAKE_CURRENT_SOURCE_DIR}/xx.po
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/xx.po
    COMMENT "Compiling reverblabr-ctl's xx test catalog"
    VERBATIM
)
add_custom_target(reverblabr-ctl-test-catalog DEPENDS ${_rb_ctl_catalog})

set(RB_CTL_TEST_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(RB_CTL_BUS_DIR "${CMAKE_CURRENT_BINARY_DIR}/buses")

add_executable(reverblabr-ctl-test test_ctl.cpp)
target_link_libraries(reverblabr-ctl-test PRIVATE reverblabr-ctl-core reverblabr-enginebus Qt6::DBus GTest::gtest)
set_target_properties(reverblabr-ctl-test PROPERTIES AUTOMOC ON)
target_compile_definitions(reverblabr-ctl-test PRIVATE
    RB_CTL="$<TARGET_FILE:reverblabr-ctl>"
    RB_CTL_TEST_LOCALEDIR="${RB_CTL_TEST_LOCALEDIR}"
    RB_CTL_TEST_DIR="${RB_CTL_TEST_DIR}"
    RB_CTL_EMPTY_BUS_CONF="${RB_CTL_BUS_DIR}/empty.conf"
    RB_DBUS_RUN_SESSION="${RB_DBUS_RUN_SESSION}")
add_dependencies(reverblabr-ctl-test reverblabr-ctl reverblabrd reverblabr-ctl-test-catalog)

# Three buses: the mock engine (sessions run without PipeWire), the real
# engine (sessions on disk), and one with no engine at all.
file(GENERATE OUTPUT "${RB_CTL_BUS_DIR}/mock-services/io.github.reverblabr.Engine.service" CONTENT
"[D-BUS Service]
Name=io.github.reverblabr.Engine
Exec=$<TARGET_FILE:reverblabrd> --mock
")
file(GENERATE OUTPUT "${RB_CTL_BUS_DIR}/engine-services/io.github.reverblabr.Engine.service" CONTENT
"[D-BUS Service]
Name=io.github.reverblabr.Engine
Exec=$<TARGET_FILE:reverblabrd>
")
file(MAKE_DIRECTORY "${RB_CTL_BUS_DIR}/empty-services")
foreach(_rb_bus mock engine empty)
    file(WRITE "${RB_CTL_BUS_DIR}/${_rb_bus}.conf"
"<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"
 \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <servicedir>${RB_CTL_BUS_DIR}/${_rb_bus}-services</servicedir>
  <policy context=\"default\">
    <allow send_destination=\"*\" eavesdrop=\"true\"/>
    <allow eavesdrop=\"true\"/>
    <allow own=\"*\"/>
  </policy>
</busconfig>
")
endforeach()

# **A PID namespace of its own**, as bus.engine has (tests/bus): the tests
# stop daemons by pid, and inside the namespace a pid gone wrong reaches
# nothing else.
set(RB_CTL_SANDBOX)
find_program(RB_UNSHARE unshare)
if(RB_UNSHARE)
    set(_rb_unshare_args --user --map-current-user --pid --fork --kill-child --mount-proc)
    execute_process(COMMAND ${RB_UNSHARE} ${_rb_unshare_args} true
                    RESULT_VARIABLE _rb_unshare_result OUTPUT_QUIET ERROR_QUIET)
    if(_rb_unshare_result EQUAL 0)
        set(RB_CTL_SANDBOX ${RB_UNSHARE} ${_rb_unshare_args})
    endif()
endif()

set(RB_CTL_HOME "${CMAKE_CURRENT_BINARY_DIR}/home")
file(MAKE_DIRECTORY "${RB_CTL_HOME}/data" "${RB_CTL_HOME}/config" "${RB_CTL_HOME}/state" "${RB_CTL_HOME}/cache")
# A runtime directory of their own, so a daemon started here never notes in
# the desktop's that Auto Run has run (ADR-0023).
file(MAKE_DIRECTORY "${RB_CTL_HOME}/runtime")
file(CHMOD "${RB_CTL_HOME}/runtime" DIRECTORY_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
set(_rb_ctl_environment
    "HOME=${RB_CTL_HOME};XDG_DATA_HOME=${RB_CTL_HOME}/data;XDG_CONFIG_HOME=${RB_CTL_HOME}/config;XDG_STATE_HOME=${RB_CTL_HOME}/state;XDG_CACHE_HOME=${RB_CTL_HOME}/cache;XDG_RUNTIME_DIR=${RB_CTL_HOME}/runtime;PIPEWIRE_REMOTE=rb-test-no-such-socket;LANG=C.UTF-8;LANGUAGE=")

add_test(NAME ctl.mock
         COMMAND ${RB_CTL_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_CTL_BUS_DIR}/mock.conf
                 -- $<TARGET_FILE:reverblabr-ctl-test> --gtest_filter=CtlMock.*)
add_test(NAME ctl.engine
         COMMAND ${RB_CTL_SANDBOX} ${RB_DBUS_RUN_SESSION} --config-file=${RB_CTL_BUS_DIR}/engine.conf
                 -- $<TARGET_FILE:reverblabr-ctl-test> --gtest_filter=CtlEngine.*:CtlReach.*)
set_tests_properties(ctl.mock ctl.engine PROPERTIES
    TIMEOUT 240
    ENVIRONMENT "${_rb_ctl_environment}"
    # A filter that matched nothing would pass; this is what says it ran.
    PASS_REGULAR_EXPRESSION "\\[  PASSED  \\] [1-9][0-9]* tests?\\."
    FAIL_REGULAR_EXPRESSION "\\[  FAILED  \\]"
)

# The shell completions and the man page, without a bus.
find_program(RB_BASH bash REQUIRED)
add_test(NAME ctl.completion-and-manual
         COMMAND ${RB_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/test_completion.sh ${PROJECT_SOURCE_DIR}/ctl)
set_tests_properties(ctl.completion-and-manual PROPERTIES
    TIMEOUT 60
    PASS_REGULAR_EXPRESSION "PASS: completion and manual"
)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# PipeWire integration tests. Every test binary runs inside the private
# instance of harness/run-private-pipewire.sh, never against a real session.

# Helpers for integration tests: graph snapshots, waits, fake apps, level taps.
add_library(reverblabr-pwtest STATIC
    capturenode.cpp
    capturenode.h
    pwtest.cpp
    pwtest.h
)
target_include_directories(reverblabr-pwtest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-pwtest PUBLIC GTest::gtest Qt6::Core PRIVATE PkgConfig::PIPEWIRE)

set(RB_PW_HARNESS ${CMAKE_CURRENT_SOURCE_DIR}/harness/run-private-pipewire.sh)

add_executable(reverblabr-pw-harness-test test_harness.cpp)
target_link_libraries(reverblabr-pw-harness-test PRIVATE reverblabr-pwtest GTest::gtest_main)

# The evidence behind ADR 0007: how capture and linking behave.
add_executable(reverblabr-pw-capture-test test_capture_semantics.cpp)
target_link_libraries(reverblabr-pw-capture-test PRIVATE reverblabr-pwtest GTest::gtest_main)
target_compile_definitions(reverblabr-pw-capture-test PRIVATE RB_PW_HARNESS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness")
add_test(NAME pw.capture-semantics COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-capture-test>)
set_tests_properties(pw.capture-semantics PROPERTIES TIMEOUT 180)

# The engine's own connection and registry mirror, judged against
# pw-dump, which watches the same graph without going through that code.
add_executable(reverblabr-pw-registry-test test_registry.cpp)
# QSignalSpy lives in QtTest.
target_link_libraries(reverblabr-pw-registry-test PRIVATE reverblabr-pwtest reverblabr-pw Qt6::Test GTest::gtest_main)
set_target_properties(reverblabr-pw-registry-test PROPERTIES AUTOMOC ON)
add_test(NAME pw.registry COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-registry-test>)
set_tests_properties(pw.registry PROPERTIES TIMEOUT 300)

# The engine's link manager. The helper is a separate process, because
# "our links die with us" cannot be checked from inside the process that made
# them.
add_executable(reverblabr-link-helper link_helper.cpp)
target_link_libraries(reverblabr-link-helper PRIVATE reverblabr-pw Qt6::Core)
set_target_properties(reverblabr-link-helper PROPERTIES AUTOMOC ON)

add_executable(reverblabr-pw-link-test test_linkmanager.cpp)
target_link_libraries(reverblabr-pw-link-test PRIVATE reverblabr-pwtest reverblabr-pw Qt6::Test GTest::gtest_main)
set_target_properties(reverblabr-pw-link-test PROPERTIES AUTOMOC ON)
target_compile_definitions(reverblabr-pw-link-test PRIVATE RB_LINK_HELPER="$<TARGET_FILE:reverblabr-link-helper>")
add_dependencies(reverblabr-pw-link-test reverblabr-link-helper)
add_test(NAME pw.links COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-link-test>)
set_tests_properties(pw.links PROPERTIES TIMEOUT 300)

# The session processing node: what one audio cycle does, judged by a
# capture filter that shares no code with the engine, and by pw-dump.
add_executable(reverblabr-pw-session-test
    scenario.h
    sessionharness.cpp
    sessionharness.h
    test_capture.cpp
    test_devices.cpp
    test_system.cpp
    test_engine.cpp
    test_inventory.cpp
    test_liveupdates.cpp
    test_pluginsessions.cpp
    test_preview.cpp
    test_recording.cpp
    test_scenarios.cpp
    test_sessionnode.cpp
    test_sessionruntime.cpp
    test_soak.cpp
    test_structuralparams.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../unit/dsp/gainblock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../unit/pw/sessionblocks.cpp
)
pkg_check_modules(RB_PW_TEST_SNDFILE REQUIRED IMPORTED_TARGET sndfile)
# The Speech Denoise scenario, where the build has the block (ADR-0016).
if(RB_BUILD_SPEECH_DENOISE)
    target_compile_definitions(reverblabr-pw-session-test PRIVATE RB_HAVE_RNNOISE)
endif()
# A Broadcast block in a running engine, where the build has one, against
# the stream tests' own server.
if(RB_BUILD_BROADCAST)
    target_sources(reverblabr-pw-session-test PRIVATE test_broadcast.cpp)
    target_include_directories(reverblabr-pw-session-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../unit/stream)
    # Its title from a media player of the test's own, on the harness's
    # private bus (tests/bus).
    target_compile_definitions(reverblabr-pw-session-test PRIVATE
        RB_FAKE_MPRIS_PLAYER="$<TARGET_FILE:reverblabr-fake-mpris-player>")
    add_dependencies(reverblabr-pw-session-test reverblabr-fake-mpris-player)
    target_link_libraries(reverblabr-pw-session-test PRIVATE Qt6::DBus)
endif()
# The CLAP sessions, where the build hosts CLAP plugins.
if(RB_BUILD_CLAP)
    target_compile_definitions(reverblabr-pw-session-test PRIVATE RB_HAVE_CLAP)
endif()
target_link_libraries(reverblabr-pw-session-test
    PRIVATE reverblabr-pwtest reverblabr-pw reverblabr-engine Qt6::Test GTest::gtest_main PkgConfig::RB_PW_TEST_SNDFILE
            PkgConfig::PIPEWIRE)
# The sessions with plugins in them start the real plugin host.
target_compile_definitions(reverblabr-pw-session-test PRIVATE RB_PLUGIN_HOST="$<TARGET_FILE:reverblabr-plugin-host>")
add_dependencies(reverblabr-pw-session-test reverblabr-plugin-host)
# LADSPA plugins in sessions, from the library tests/ladspa builds.
target_compile_definitions(reverblabr-pw-session-test PRIVATE RB_TEST_LADSPA_DIR="${RB_TEST_LADSPA_DIR}")
add_dependencies(reverblabr-pw-session-test rb-test-ladspa)
# The notification helper lives beside the harness script.
target_compile_definitions(reverblabr-pw-session-test PRIVATE RB_PW_HARNESS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/harness")
set_target_properties(reverblabr-pw-session-test PROPERTIES AUTOMOC ON)
# The inventory rides in the same binary rather than one of its own:
# another copy of the harness is another two minutes of build and another
# gigabyte of compiler, and the CI runner has neither to spare. The two are
# separate ctest entries all the same, so a failure names the right thing and
# the private instances do not have to share.
add_test(NAME pw.session-node COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=-Inventories.*:Capture.*:Devices.*:SystemAudio.*:Structural.*:Scenarios.*:Recording.*:PluginSessions.*:Preview.*:Soak.*:SessionCost.*)
# The soak inside it runs for RB_SOAK_SECONDS, a minute by default.
set_tests_properties(pw.session-node PROPERTIES TIMEOUT 600)

# **The give-way tests on a data loop that is not real-time**. rtkit
# refuses real-time once a user has asked too often too quickly, and a busy
# gate reaches that; RB_PW_NO_REALTIME leaves PipeWire's rt module out, so
# this case is tried every time rather than whenever rtkit says no.
add_test(NAME pw.give-way-no-realtime COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=SessionNodes.ASessionOverrunningEveryCycleGivesWayAndTheProcessLives:Runtime.ASessionThatGivesWayToKeepTheLoopAliveSaysWhichBlockCostTheMost)
set_tests_properties(pw.give-way-no-realtime PROPERTIES TIMEOUT 300 ENVIRONMENT "RB_PW_NO_REALTIME=1")


add_test(NAME pw.inventory COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=Inventories.*)
set_tests_properties(pw.inventory PROPERTIES TIMEOUT 300)

# Capturing an application: the same binary again, because another one
# that includes the harness is another gigabyte of compiler on the CI runner.
add_test(NAME pw.capture COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=Capture.*)
set_tests_properties(pw.capture PROPERTIES TIMEOUT 600)

# The device blocks: where the sound goes and where it comes from.
add_test(NAME pw.devices COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=Devices.*)
set_tests_properties(pw.devices PROPERTIES TIMEOUT 600)

# System Audio: everything that is playing, and what is left out of it.
add_test(NAME pw.system COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=SystemAudio.*)
set_tests_properties(pw.system PROPERTIES TIMEOUT 600)

# Structural parameters: a device or a capture rule chosen on a
# session that is already running, and the links moving with it.
add_test(NAME pw.structural-params COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=Structural.*)
set_tests_properties(pw.structural-params PROPERTIES TIMEOUT 600)

# The sessions M1 is judged by: the main blocks together, built
# through the facade, and what the busiest of them costs.
add_test(NAME pw.scenarios COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=Scenarios.*)
set_tests_properties(pw.scenarios PROPERTIES TIMEOUT 600)

# Virtual devices (ADR-0013): the drop-in read by a real daemon.
# **Not under the pipewire-audio lock**: what it checks is that every cycle
# the session ran reached the files, and a cycle the server never ran under
# load is one no Recorder was given — so load cannot fail it, and it need
# not wait in the queue of the tests it could.
add_test(NAME pw.recording COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=Recording.*)
set_tests_properties(pw.recording PROPERTIES TIMEOUT 300)

# Sessions with plugins in them (ADR-0014): cut into segments around
# each plugin's host, judged by two devices of the test's own. **Not under
# the pipewire-audio lock**, like pw.plugin-host: it counts cycles and reads
# codes back, which load delays but does not change.
add_test(NAME pw.plugin-sessions COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=PluginSessions.*)
set_tests_properties(pw.plugin-sessions PROPERTIES TIMEOUT 300)

# Listening to a recording beside a running session.
add_test(NAME pw.preview COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=Preview.*)
set_tests_properties(pw.preview PROPERTIES TIMEOUT 300)

# A Broadcast block in a running engine: its password from a store in
# memory, its stream to a server the test makes, nothing on the network.
if(RB_BUILD_BROADCAST)
    add_test(NAME pw.broadcast COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
             --gtest_filter=Broadcasting.*)
    set_tests_properties(pw.broadcast PROPERTIES TIMEOUT 300)
endif()

add_executable(reverblabr-pw-virtualdevices-test test_virtualdevices.cpp)
target_link_libraries(reverblabr-pw-virtualdevices-test PRIVATE reverblabr-pwtest reverblabr-pw GTest::gtest_main)
add_test(NAME pw.virtual-devices COMMAND ${RB_PW_HARNESS} --no-pulse $<TARGET_FILE:reverblabr-pw-virtualdevices-test>)
set_tests_properties(pw.virtual-devices PROPERTIES TIMEOUT 120)

# The virtual device manager: the files and the live graph in step.
add_executable(reverblabr-pw-virtualdevicemanager-test test_virtualdevicemanager.cpp)
target_link_libraries(reverblabr-pw-virtualdevicemanager-test
    PRIVATE reverblabr-pwtest reverblabr-pw GTest::gtest_main)
add_test(NAME pw.virtual-device-manager COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-virtualdevicemanager-test>)
set_tests_properties(pw.virtual-device-manager PROPERTIES TIMEOUT 180)

# A plugin in a process of its own (ADR-0014): same-cycle scheduling
# through it, parameters through its ring, and a host that dies or stops.
# **Not under the pipewire-audio lock**: it counts cycles and compares codes,
# which load delays but does not change, and its time limits (100 ms to
# notice a host gone) are ten times what it measures.
add_executable(reverblabr-pw-pluginhost-test test_pluginhost.cpp)
target_link_libraries(reverblabr-pw-pluginhost-test
    PRIVATE reverblabr-pwtest reverblabr-pw PkgConfig::PIPEWIRE GTest::gtest_main)
target_compile_definitions(reverblabr-pw-pluginhost-test PRIVATE RB_PLUGIN_HOST="$<TARGET_FILE:reverblabr-plugin-host>")
add_dependencies(reverblabr-pw-pluginhost-test reverblabr-plugin-host)
# LADSPA plugins in the host, from the library tests/ladspa builds.
target_compile_definitions(reverblabr-pw-pluginhost-test PRIVATE RB_TEST_LADSPA_DIR="${RB_TEST_LADSPA_DIR}")
add_dependencies(reverblabr-pw-pluginhost-test rb-test-ladspa)
add_test(NAME pw.plugin-host COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-pluginhost-test>)
set_tests_properties(pw.plugin-host PROPERTIES TIMEOUT 180)

add_test(NAME pw.harness COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-harness-test>)
set_tests_properties(pw.harness PROPERTIES TIMEOUT 180)

# A failing test must print a pw-dump of the private graph.
add_test(NAME pw.dump-on-failure
    COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-harness-test>
            --gtest_also_run_disabled_tests
            --gtest_filter=Harness.DISABLED_AFailureReportIncludesAGraphDump
)
set_tests_properties(pw.dump-on-failure PROPERTIES
    PASS_REGULAR_EXPRESSION "pw-dump of the private graph at the first failure of Harness[.]DISABLED_AFailureReportIncludesAGraphDump ----[^\n]*\n[[]"
    TIMEOUT 60
)

# **The tests that judge the sound do not run at the same time as each
# other.** Each one starts a PipeWire instance of its own, and three of them
# measuring levels and counting xruns on one machine is a test of the machine
# rather than of the code: pw.session-node failed once in a parallel run and
# passed on its own twice. They still run beside everything else.
#
# **pw.inventory is one of them**: it times how long a device or an
# application takes to be listed against the quarter second allowed, and
# failed at 253 ms of 250 beside the others under asan, having passed every
# time on its own.
set_tests_properties(pw.session-node pw.capture pw.devices pw.system pw.structural-params pw.scenarios pw.inventory
    pw.preview pw.give-way-no-realtime
    PROPERTIES RESOURCE_LOCK pipewire-audio)

# **Every test here starts a PipeWire of its own**, and carries the label
# that says so. A run that has no daemon to spare — the rtsan-offline
# preset in the gate — leaves them out by label. Not by name: the unit tests
# of the pw layer are called pw.SessionProcessorTest.* and so on, need no
# daemon, and are exactly the ones a real-time check must not skip.
get_property(rb_pw_daemon_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${rb_pw_daemon_tests} PROPERTIES LABELS pipewire)

# **The long runs and what a session costs** (test_soak.cpp), which no
# ordinary run includes: the soak's figures are only worth having from a
# machine that does nothing else, and the costs only from an optimised
# build. The test presets leave the labels soak and perf out; the perf
# preset runs them, short, and docs/development.md ("Soak and performance
# runs") says how to run the soak for hours. Their figures go to
# perf-results/ in the build tree, for tools/perf/report.py.
add_test(NAME pw.soak COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test> --gtest_filter=Soak.*)
add_test(NAME pw.session-cost COMMAND ${RB_PW_HARNESS} $<TARGET_FILE:reverblabr-pw-session-test>
         --gtest_filter=SessionCost.*)
set_tests_properties(pw.soak pw.session-cost PROPERTIES
    TIMEOUT 600
    RESOURCE_LOCK pipewire-audio
    ENVIRONMENT "RB_PERF_OUT=${CMAKE_BINARY_DIR}/perf-results"
)
set_tests_properties(pw.soak PROPERTIES LABELS "pipewire;soak")
set_tests_properties(pw.session-cost PROPERTIES LABELS "pipewire;perf")

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

include(GoogleTest)

# rb_add_gtest(<target> LAYER <name> SOURCES <file>... LIBRARIES <target>...)
#
# Builds a GoogleTest executable and registers each test case with CTest as
# <layer>.<Suite>.<Test>.
# **A test never writes into the home directory of whoever runs it.**
# Anything that asks QStandardPaths where to put application data — a preset
# store, a config, a cache — is answered with a directory under the build
# tree instead. Found the hard way: a preset test with a default-constructed
# store left a file in ~/.local/share. Setting it here covers every
# test rather than every test remembering.
set(RB_TEST_HOME "${CMAKE_CURRENT_BINARY_DIR}/test-home")
# The four folders too, so that no test depends on another having run first
# and made one; a test that needs one still makes it itself.
file(MAKE_DIRECTORY "${RB_TEST_HOME}/data" "${RB_TEST_HOME}/config" "${RB_TEST_HOME}/state" "${RB_TEST_HOME}/cache")
set(RB_TEST_ENVIRONMENT
    "XDG_DATA_HOME=${RB_TEST_HOME}/data"
    "XDG_CONFIG_HOME=${RB_TEST_HOME}/config"
    "XDG_STATE_HOME=${RB_TEST_HOME}/state"
    "XDG_CACHE_HOME=${RB_TEST_HOME}/cache"
)
# **Escape the list's own separators once more** than set_tests_properties
# would need. gtest_discover_tests expands PROPERTIES an extra time on its way
# to the tests, so a plain list of four came out as ENVIRONMENT with the first
# entry alone, the next as a property *named* XDG_CONFIG_HOME=…, and the last
# dropped: every unit test ran with the real ~/.config, ~/.local/state and
# ~/.cache. core/test_testhome.cpp checks all four arrive.
string(REPLACE ";" "\\;" RB_TEST_ENVIRONMENT_ESCAPED "${RB_TEST_ENVIRONMENT}")

function(rb_add_gtest target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LAYER" "SOURCES;LIBRARIES")
    add_executable(${target} ${arg_SOURCES})
    target_link_libraries(${target} PRIVATE ${arg_LIBRARIES} GTest::gtest_main)
    gtest_discover_tests(${target}
        TEST_PREFIX "${arg_LAYER}."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES ENVIRONMENT "${RB_TEST_ENVIRONMENT_ESCAPED}"
    )
endfunction()

# reverblabr-ctl's own logic, without a bus; its commands against a
# daemon are tests/ctl.
rb_add_gtest(reverblabr-ctl-unit-test
    LAYER ctl
    SOURCES
        ctl/test_lookup.cpp
    LIBRARIES
        reverblabr-ctl-core
)

rb_add_gtest(reverblabr-dsp-test
    LAYER dsp
    SOURCES
        dsp/earblock.cpp
        dsp/earblock.h
        dsp/gainblock.cpp
        dsp/gainblock.h
        dsp/kitblocks.cpp
        dsp/kitblocks.h
        dsp/test_audiobuffer.cpp
        dsp/test_autoregressive.cpp
        dsp/test_biquad.cpp
        dsp/test_block.cpp
        dsp/test_blockregistry.cpp
        dsp/test_channelmap.cpp
        dsp/test_crossover.cpp
        dsp/test_decibel.cpp
        dsp/test_delayline.cpp
        dsp/test_denormals.cpp
        dsp/test_dynamics.cpp
        dsp/test_dspkit.cpp
        dsp/test_ear.cpp
        dsp/test_envelope.cpp
        dsp/test_fft.cpp
        dsp/test_fittingrule.cpp
        dsp/test_frequencymove.cpp
        dsp/test_hearingbands.cpp
        dsp/test_humtracker.cpp
        dsp/test_limiter.cpp
        dsp/test_loudness.cpp
        dsp/test_paramdescriptor.cpp
        dsp/test_paramstore.cpp
        dsp/test_paramvalue.cpp
        dsp/test_smoothing.cpp
        dsp/test_spectrum.cpp
        dsp/test_truepeak.cpp
        dsp/test_stereogains.cpp
    LIBRARIES reverblabr-dsp reverblabr-dsp-testkit
)
set_target_properties(reverblabr-dsp-test PROPERTIES AUTOMOC OFF AUTORCC OFF)
# The reviewed tables of what the fitting rule gives the standard audiograms.
target_compile_definitions(reverblabr-dsp-test PRIVATE RB_FITTING_TABLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/dsp/fitting")

rb_add_gtest(reverblabr-rt-test
    LAYER rt
    SOURCES
        rt/test_audiotap.cpp
        rt/test_logger.cpp
        rt/test_meter.cpp
        rt/test_retirequeue.cpp
        rt/test_runwatch.cpp
        rt/test_seqlock.cpp
        rt/test_spscqueue.cpp
        rt/test_triplebuffer.cpp
    LIBRARIES reverblabr-rt
)
set_target_properties(reverblabr-rt-test PROPERTIES AUTOMOC OFF AUTORCC OFF)

rb_add_gtest(reverblabr-blocks-test
    LAYER blocks
    SOURCES
        dsp/earblock.cpp
        dsp/earblock.h
        blocks/test_applicationsource.cpp
        blocks/test_autolevel.cpp
        blocks/test_balance.cpp
        blocks/test_basstreble.cpp
        blocks/test_channels.cpp
        blocks/test_declick.cpp
        blocks/test_denoise.cpp
        blocks/test_ducking.cpp
        blocks/test_dehum.cpp
        blocks/test_dynamics.cpp
        blocks/test_echo.cpp
        blocks/test_equalizer10.cpp
        blocks/test_firfilter.cpp
        blocks/test_frequencylowering.cpp
        blocks/test_frequencyraising.cpp
        blocks/test_highpassfilter.cpp
        blocks/test_inputdevice.cpp
        blocks/test_inputswitch.cpp
        blocks/test_hearingcompensation.cpp
        blocks/test_hearingcompensation64.cpp
        blocks/test_joinlr.cpp
        blocks/test_limiter.cpp
        blocks/test_lowpassfilter.cpp
        blocks/test_mixer.cpp
        blocks/test_monoenhancer.cpp
        blocks/test_multibandcompressor.cpp
        blocks/test_outputdevice.cpp
        blocks/test_pan.cpp
        blocks/test_parametriceq.cpp
        blocks/test_peakrmsmeter.cpp
        blocks/test_pitchshift.cpp
        blocks/test_pluginblock.cpp
        blocks/test_recorder.cpp
        blocks/test_reverb.cpp
        blocks/test_simplecompressor.cpp
        blocks/test_sync.cpp
        blocks/test_systemaudio.cpp
        blocks/test_timeshift.cpp
        blocks/test_volume.cpp
        blocks/test_vumeter.cpp
        blocks/test_wave.cpp
    LIBRARIES reverblabr-blocks reverblabr-dsp-testkit
)
# Speech Denoise, where the build has RNNoise (ADR-0016).
if(RB_BUILD_SPEECH_DENOISE)
    target_sources(reverblabr-blocks-test PRIVATE blocks/test_speechdenoise.cpp)
endif()
# Broadcast, where the build can keep its password (ADR-0026).
if(RB_BUILD_BROADCAST)
    target_sources(reverblabr-blocks-test PRIVATE blocks/test_broadcast.cpp)
endif()
# Live Stream, where the build has it (ADR-0027).
if(RB_BUILD_LIVESTREAM)
    target_sources(reverblabr-blocks-test PRIVATE blocks/test_livestream.cpp)
endif()
set_target_properties(reverblabr-blocks-test PROPERTIES AUTOMOC OFF AUTORCC OFF)

# Recordings on the disk. Read back through libsndfile directly, which
# is what every other program that opens one of these files will be using.
pkg_check_modules(RB_TEST_SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(RB_TEST_TAGLIB REQUIRED IMPORTED_TARGET taglib)
pkg_check_modules(RB_TEST_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
rb_add_gtest(reverblabr-record-test
    LAYER record
    SOURCES
        record/recordfixtures.h
        record/test_audiofile.cpp
        record/test_audiofilereader.cpp
        record/test_filename.cpp
        record/test_impulsefile.cpp
        record/test_m4a.cpp
        record/test_recordingwriter.cpp
        record/test_repair.cpp
        record/test_summary.cpp
        record/test_tags.cpp
    LIBRARIES reverblabr-record reverblabr-rt PkgConfig::RB_TEST_SNDFILE PkgConfig::RB_TEST_TAGLIB
              PkgConfig::RB_TEST_FFMPEG
)
set_target_properties(reverblabr-record-test PROPERTIES AUTOMOC OFF AUTORCC OFF)

# Streams to an internet radio server, against a server the test makes
# (fakeserver.h), and read back through FFmpeg as a player reads them. A
# real Icecast is tests/broadcast, outside the default run.
rb_add_gtest(reverblabr-stream-test
    LAYER stream
    SOURCES
        stream/fakeserver.h
        stream/streamdecode.h
        stream/test_backoff.cpp
        stream/test_broadcaststream.cpp
        stream/test_protocol.cpp
        stream/test_streamencoder.cpp
        stream/test_title.cpp
    LIBRARIES reverblabr-stream reverblabr-rt PkgConfig::RB_TEST_FFMPEG
)
set_target_properties(reverblabr-stream-test PROPERTIES AUTOMOC OFF AUTORCC OFF)

# A live stream (ADR-0027): the picture, the encoders, and the thread that
# streams, against FFmpeg's own RTMP server run in the test. A real server
# is tests/livestream, outside the default run.
if(RB_BUILD_LIVESTREAM)
    rb_add_gtest(reverblabr-livestream-test
        LAYER stream
        SOURCES
            stream/rtmplistener.h
            stream/test_compositor.cpp
            stream/test_livestream.cpp
            stream/test_videoencoder.cpp
        LIBRARIES reverblabr-livestream reverblabr-stream reverblabr-rt PkgConfig::RB_TEST_FFMPEG
    )
    set_target_properties(reverblabr-livestream-test PROPERTIES AUTOMOC OFF AUTORCC OFF)
endif()

# What a media player says, and which one a stream's title follows,
# without a bus; the players on one are tests/bus.
rb_add_gtest(reverblabr-mpris-test
    LAYER mpris
    SOURCES
        mpris/test_player.cpp
    LIBRARIES reverblabr-mpris
)

# What an engine and a plugin host share, and one cycle of the host.
rb_add_gtest(reverblabr-plugin-test
    LAYER plugin
    SOURCES
        plugin/test_formats.cpp
        plugin/test_ladspa.cpp
        plugin/test_lv2window.cpp
        plugin/test_pluginproto.cpp
    LIBRARIES reverblabr-pluginproto reverblabr-pluginhost-ladspa reverblabr-pluginhost-formats reverblabr-dsp-testkit
)
set_target_properties(reverblabr-plugin-test PROPERTIES AUTOMOC OFF AUTORCC OFF)
# LADSPA plugins run as blocks, from the library tests/ladspa builds.
target_compile_definitions(reverblabr-plugin-test PRIVATE RB_TEST_LADSPA_DIR="${RB_TEST_LADSPA_DIR}")
add_dependencies(reverblabr-plugin-test rb-test-ladspa)
# The tests' LV2 plugin and its window, from tests/lv2.
target_compile_definitions(reverblabr-plugin-test PRIVATE RB_TEST_LV2_DIR="${RB_TEST_LV2_DIR}")
add_dependencies(reverblabr-plugin-test rb-test-lv2 rb-test-lv2-ui)
# The tests' CLAP plugin and its stand-in window, where the build hosts CLAP
# plugins.
if(RB_BUILD_CLAP)
    target_sources(reverblabr-plugin-test PRIVATE plugin/test_clapwindow.cpp)
    target_compile_definitions(reverblabr-plugin-test PRIVATE RB_TEST_CLAP_DIR="${RB_TEST_CLAP_DIR}")
    add_dependencies(reverblabr-plugin-test rb-test-clap)
endif()
# The tests' VST3 plugin and its stand-in view, where the build hosts VST3
# plugins.
if(RB_BUILD_VST3)
    target_sources(reverblabr-plugin-test PRIVATE plugin/test_vst3plugin.cpp)
    target_compile_definitions(reverblabr-plugin-test PRIVATE RB_TEST_VST3_DIR="${RB_TEST_VST3_DIR}")
    add_dependencies(reverblabr-plugin-test rb-test-vst3)
endif()

rb_add_gtest(reverblabr-graph-test
    LAYER graph
    SOURCES
        graph/test_bypass.cpp
        graph/test_compiledgraph.cpp
        graph/test_graphdiff.cpp
        graph/test_latencycompensation.cpp
        graph/test_segments.cpp
        graph/test_topology.cpp
        graph/testblocks.cpp
        graph/testblocks.h
    LIBRARIES reverblabr-graph reverblabr-dsp-testkit
)
set_target_properties(reverblabr-graph-test PROPERTIES AUTOMOC OFF AUTORCC OFF)

rb_add_gtest(reverblabr-pw-test
    LAYER pw
    SOURCES
        dsp/gainblock.cpp
        dsp/gainblock.h
        pw/pwdump.cpp
        pw/pwdump.h
        pw/sessionblocks.cpp
        pw/sessionblocks.h
        pw/test_feedbackguard.cpp
        pw/test_inventory.cpp
        pw/test_sessionprocessor.cpp
        pw/test_version.cpp
        pw/test_virtualdeviceconf.cpp
        pw/test_virtualdevicemanager.cpp
    LIBRARIES reverblabr-pw reverblabr-blocks reverblabr-dsp-testkit
)
set_target_properties(reverblabr-pw-test PROPERTIES AUTOMOC OFF AUTORCC OFF)
# Graphs written out the way pw-dump writes them, for devices no test
# machine has.
target_compile_definitions(reverblabr-pw-test PRIVATE RB_PW_GRAPHS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/pw/graphs")

# What the all-users helper does as root, whether KAuth is here or not.
rb_add_gtest(reverblabr-helper-test
    LAYER helper
    SOURCES
        helper/test_systemdevicesaction.cpp
    LIBRARIES reverblabr-helper-action
)
set_target_properties(reverblabr-helper-test PROPERTIES AUTOMOC OFF AUTORCC OFF)

# What is worth a desktop notification, and its words.
rb_add_gtest(reverblabr-notify-test
    LAYER notify
    SOURCES
        notify/test_notifications.cpp
    LIBRARIES reverblabr-notify Qt6::Test
)
target_compile_definitions(reverblabr-notify-test PRIVATE
    RB_NOTIFYRC="${PROJECT_SOURCE_DIR}/notify/reverblabr.notifyrc"
    RB_APP_DIR="${PROJECT_SOURCE_DIR}/app")

rb_add_gtest(reverblabr-engine-test
    LAYER engine
    SOURCES
        dsp/earblock.cpp
        dsp/earblock.h
        core/sessionfixtures.h
        core/test_audiogramfit.cpp
        core/test_sessionplan.cpp
        core/test_blockanalysis.cpp
        core/test_blockcatalogue.cpp
        core/test_compressormodes.cpp
        core/test_diagnostics.cpp
        core/test_editcommand.cpp
        core/test_editcommandjson.cpp
        core/test_edithistory.cpp
        core/test_enginefacade.cpp
        core/test_engineautosave.cpp
        core/test_engineinfo.cpp
        core/test_errorcodes.cpp
        core/test_logring.cpp
        core/test_mockengine.cpp
        core/test_pluginscanner.cpp
        core/test_presetactions.cpp
        core/test_presetstore.cpp
        core/test_quitapplications.cpp
        core/test_recordinglibrary.cpp
        core/test_recordings.cpp
        core/test_session.cpp
        core/test_sessionlist.cpp
        core/test_sessionstore.cpp
        core/test_sharedtelemetry.cpp
        core/test_systemdevicechange.cpp
        core/test_telemetry.cpp
        core/test_testhome.cpp
        core/test_timerrunner.cpp
        core/test_timers.cpp
        core/test_virtualdevicefacade.cpp
        core/test_xrunhistory.cpp
    LIBRARIES reverblabr-engine reverblabr-pw reverblabr-blocks reverblabr-record reverblabr-helper-action Qt6::Xml
              Qt6::Test
)
target_compile_definitions(reverblabr-engine-test PRIVATE
    RB_ENGINE_INTERFACE_XML="${RB_ENGINE_INTERFACE_XML}"
    RB_TEST_HOME="${RB_TEST_HOME}"
)
# It must also be a D-Bus interface, not merely an XML file that looks like one.
# What the engine makes of a Broadcast block, where the build has one.
if(RB_BUILD_BROADCAST)
    target_sources(reverblabr-engine-test PRIVATE core/test_broadcasts.cpp)
endif()
if(RB_BUILD_LIVESTREAM)
    target_sources(reverblabr-engine-test PRIVATE core/test_livestreams.cpp)
endif()
add_dependencies(reverblabr-engine-test reverblabr-engine-interface)

# The plugin scanner describes with the real plugin host.
target_compile_definitions(reverblabr-engine-test PRIVATE RB_PLUGIN_HOST="$<TARGET_FILE:reverblabr-plugin-host>")
add_dependencies(reverblabr-engine-test reverblabr-plugin-host)
# And LADSPA's against the libraries tests/ladspa builds.
target_compile_definitions(reverblabr-engine-test PRIVATE
    RB_TEST_LADSPA_DIR="${RB_TEST_LADSPA_DIR}"
    RB_TEST_LADSPA_CRASH_DIR="${RB_TEST_LADSPA_CRASH_DIR}"
)
add_dependencies(reverblabr-engine-test rb-test-ladspa rb-test-ladspa-crash)

rb_add_gtest(reverblabr-app-test
    LAYER app
    SOURCES
        app/test_allusersdevices.cpp
        app/test_appearance.cpp
        app/test_applicationicon.cpp
        app/test_applicationsource.cpp
        app/test_appshell.cpp
        app/test_autorunsetting.cpp
        app/test_sessions.cpp
        app/test_audiosystem.cpp
        app/test_blockmodel.cpp
        app/test_blockpresentation.cpp
        app/test_dynamicspanel.cpp
        app/test_equalizerplot.cpp
        app/test_errormessages.cpp
        app/test_errorsurface.cpp
        app/test_diagnostics.cpp
        app/test_fileactions.cpp
        app/test_globalshortcuts.cpp
        app/test_inventory.cpp
        app/test_launchrequests.cpp
        app/test_levelmeter.cpp
        app/test_meterhub.cpp
        app/test_meters.cpp
        app/test_meterscale.cpp
        app/test_peakrmspanel.cpp
        app/test_recentapplications.cpp
        app/test_recordingstatus.cpp
        app/test_sessionmodel.cpp
        app/test_sessionoverview.cpp
        app/test_traystate.cpp
        app/test_systemaudio.cpp
        app/test_virtualdevices.cpp
        app/test_volumepanel.cpp
        app/test_waveformitem.cpp
        app/test_vuscale.cpp
        app/test_windowframe.cpp
        app/test_windowstate.cpp
        app/test_wiremodel.cpp
    # The blocks and the test kit, so that what a panel reads can be measured
    # from a signal through the real block rather than from numbers a test
    # made up. The interface itself does not depend on them.
    LIBRARIES reverblabr-ui reverblabr-blocks reverblabr-dsp-testkit Qt6::Test
)
# The desktop entry, whose actions and icon the launch and icon tests read.
target_compile_definitions(reverblabr-app-test PRIVATE
    RB_DESKTOP_FILE="${PROJECT_SOURCE_DIR}/app/io.github.reverblabr.Reverblabr.desktop")

# Copy Diagnostics against a real clipboard: a binary of its own, so
# that it makes its application on the offscreen platform before anything
# else can make one, and never writes to a desktop's clipboard.
rb_add_gtest(reverblabr-diagnostics-clipboard-test
    LAYER app
    SOURCES app/test_diagnosticsclipboard.cpp
    LIBRARIES reverblabr-ui
)

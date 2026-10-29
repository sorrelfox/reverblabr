# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The blocks a session is built from. Plain C++23 like the SDK they are
# written against: a block knows nothing about PipeWire, about sessions or
# about the interface, which is what lets every one of them be run offline
# against a signal and measured.
add_library(reverblabr-blocks STATIC
    applicationsource.cpp
    applicationsource.h
    autolevel.cpp
    autolevel.h
    balance.cpp
    balance.h
    basstreble.cpp
    basstreble.h
    builtinblocks.cpp
    builtinblocks.h
    channels.cpp
    channels.h
    cutofffilter.cpp
    cutofffilter.h
    declick.cpp
    declick.h
    dehum.cpp
    dehum.h
    denoise.cpp
    denoise.h
    ducking.cpp
    ducking.h
    dynamics.cpp
    dynamics.h
    echo.cpp
    echo.h
    firfilter.cpp
    firfilter.h
    frequencylowering.cpp
    frequencylowering.h
    frequencymoveblock.cpp
    frequencymoveblock.h
    frequencyraising.cpp
    frequencyraising.h
    equalizer10.cpp
    equalizer10.h
    hearingcompensation.cpp
    hearingcompensation.h
    highpassfilter.h
    inputdevice.cpp
    inputdevice.h
    inputswitch.cpp
    inputswitch.h
    joinlr.cpp
    joinlr.h
    limiter.cpp
    limiter.h
    lowpassfilter.h
    mixer.cpp
    mixer.h
    monoenhancer.cpp
    monoenhancer.h
    multibandcompressor.cpp
    multibandcompressor.h
    parametriceq.cpp
    parametriceq.h
    peakrmsmeter.cpp
    peakrmsmeter.h
    pitchshift.cpp
    pitchshift.h
    pluginblock.cpp
    pluginblock.h
    outputdevice.cpp
    outputdevice.h
    pan.cpp
    pan.h
    recorder.cpp
    recorder.h
    reverb.cpp
    reverb.h
    simplecompressor.cpp
    simplecompressor.h
    sync.cpp
    sync.h
    systemaudio.cpp
    systemaudio.h
    timeshift.cpp
    timeshift.h
    timeshiftspill.cpp
    timeshiftspill.h
    volume.cpp
    volume.h
    vumeter.cpp
    vumeter.h
    wave.cpp
    wave.h
)
target_include_directories(reverblabr-blocks PUBLIC ${PROJECT_SOURCE_DIR}/engine)
# The plugin block asks the plugins built into the host what they are
# (ADR-0014), which is the plugin protocol's layer and not a host. The FIR
# Filter reads its impulse response through the recordings' layer
# (libsndfile, soxr), on the engine thread.
target_link_libraries(reverblabr-blocks PUBLIC reverblabr-dsp PRIVATE reverblabr-pluginproto reverblabr-record)
# Pitch Shift runs SoundTouch (LGPL-2.1), linked dynamically.
pkg_check_modules(SOUNDTOUCH REQUIRED IMPORTED_TARGET soundtouch>=2.1)
target_link_libraries(reverblabr-blocks PRIVATE PkgConfig::SOUNDTOUCH)
# Speech Denoise runs RNNoise (ADR-0016), where the distribution has it.
if(RB_BUILD_SPEECH_DENOISE)
    target_sources(reverblabr-blocks PRIVATE speechdenoise.cpp speechdenoise.h)
    target_compile_definitions(reverblabr-blocks PUBLIC RB_HAVE_RNNOISE)
    target_link_libraries(reverblabr-blocks PRIVATE PkgConfig::RNNOISE)
endif()
# Broadcast, where the build can keep its password (ADR-0026). Its stream
# and its settings are the engine's; the block is only offered here.
if(RB_BUILD_BROADCAST)
    target_sources(reverblabr-blocks PRIVATE broadcast.cpp)
    target_compile_definitions(reverblabr-blocks PUBLIC RB_HAVE_BROADCAST)
endif()
set_target_properties(reverblabr-blocks PROPERTIES AUTOMOC OFF AUTORCC OFF)

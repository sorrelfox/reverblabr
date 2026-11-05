# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The blocks a session is built from, a folder each (ADR-0035): a block's
# C++, the interface that shows it, its icon, its presets and its tests.
# rb_add_block() (cmake/RbBlocks.cmake) hands each part of the build its
# share, so these folders are added before the engine and the interface.
#
# The C++ is plain C++23 like the SDK it is written against: a block knows
# nothing about PipeWire, about sessions or about the interface, which is
# what lets every one of them be run offline against a signal and measured.
add_library(reverblabr-blocks STATIC
    builtinblocks.cpp
    builtinblocks.h
)
# "blocks/<folder>/<file>.h" from the top, and the engine's layers as before.
target_include_directories(reverblabr-blocks PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-blocks PUBLIC reverblabr-dsp)
set_target_properties(reverblabr-blocks PROPERTIES AUTOMOC OFF AUTORCC OFF)

# What more than one block uses, and is not a block itself.
add_subdirectory(shared)

add_subdirectory(amplify)
add_subdirectory(applicationsource)
add_subdirectory(autolevel)
add_subdirectory(balance)
add_subdirectory(basstreble)
add_subdirectory(broadcast)          # and Live Stream
add_subdirectory(channels)
add_subdirectory(cutofffilter)       # Low Pass and High Pass
add_subdirectory(declick)
add_subdirectory(dehum)
add_subdirectory(denoise)
add_subdirectory(ducking)
add_subdirectory(dynamics)
add_subdirectory(echo)
add_subdirectory(equalizer10)
add_subdirectory(firfilter)
add_subdirectory(frequencymove)      # Frequency Lowering and Frequency Raising
add_subdirectory(hearingcompensation)
add_subdirectory(inputdevice)
add_subdirectory(inputswitch)
add_subdirectory(joinlr)
add_subdirectory(limiter)
add_subdirectory(mixer)
add_subdirectory(modelblock)
add_subdirectory(monoenhancer)
add_subdirectory(multibandcompressor)
add_subdirectory(nightlevel)
add_subdirectory(oneearmix)
add_subdirectory(outputdevice)
add_subdirectory(pan)
add_subdirectory(parametriceq)
add_subdirectory(peakrmsmeter)
add_subdirectory(pitchshift)
add_subdirectory(pluginblock)
add_subdirectory(recorder)
add_subdirectory(reverb)
add_subdirectory(simplecompressor)
add_subdirectory(soundside)
add_subdirectory(spectrummeter)
add_subdirectory(speechdenoise)
add_subdirectory(speechnow)
add_subdirectory(suddensounds)
add_subdirectory(sync)
add_subdirectory(systemaudio)
add_subdirectory(timeshift)
add_subdirectory(voicecompressor)
add_subdirectory(voiceforward)
add_subdirectory(voicegauge)
add_subdirectory(volume)
add_subdirectory(vumeter)
add_subdirectory(wave)
add_subdirectory(waveformmeter)

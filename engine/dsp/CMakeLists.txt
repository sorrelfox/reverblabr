# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# DSP core: plain C++23, with no Qt, PipeWire or KDE Frameworks, so it can run
# on the real-time thread and be tested offline.
# Most of these are header-only: they are used one sample at a time inside a
# process callback, where a call across a translation unit would show up in
# the cycle time.
add_library(reverblabr-dsp STATIC
    audioblock.h
    audiogram.cpp
    audiogram.h
    audiobuffer.h
    autoregressive.h
    biquad.h
    block.cpp
    block.h
    blockinfo.h
    blockregistry.cpp
    blockregistry.h
    channelmap.cpp
    channelmap.h
    crossover.cpp
    crossover.h
    decibel.cpp
    decibel.h
    delayline.h
    denormals.h
    ear.cpp
    ear.h
    envelope.h
    erbscale.cpp
    erbscale.h
    fft.cpp
    fft.h
    fittingrule.cpp
    fittingrule.h
    frequencymove.cpp
    frequencymove.h
    gammatonebank.cpp
    gammatonebank.h
    hearingbands.cpp
    hearingbands.h
    humtracker.cpp
    humtracker.h
    dynamics.cpp
    dynamics.h
    limiter.h
    loudness.h
    monotonecurve.cpp
    monotonecurve.h
    paramdescriptor.cpp
    paramdescriptor.h
    paramstore.cpp
    paramstore.h
    paramvalue.cpp
    paramvalue.h
    smoothing.h
    spectrum.cpp
    spectrum.h
    truepeak.h
    stereogains.h
)
target_include_directories(reverblabr-dsp PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-dsp PUBLIC reverblabr-rt)
set_target_properties(reverblabr-dsp PROPERTIES AUTOMOC OFF AUTORCC OFF)

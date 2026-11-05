# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# What more than one block uses and is not a block itself: a block's folder
# takes from here and from the engine's DSP, never from another block's.
target_sources(reverblabr-blocks PRIVATE
    voicemodel.cpp
    voicemodel.h
)
# RNNoise, where the build has it (ADR-0016): the voice detector here
# listens with it, and Speech Denoise cleans with it.
if(RB_BUILD_SPEECH_DENOISE)
    target_compile_definitions(reverblabr-blocks PUBLIC RB_HAVE_RNNOISE)
    target_link_libraries(reverblabr-blocks PRIVATE PkgConfig::RNNOISE)
endif()

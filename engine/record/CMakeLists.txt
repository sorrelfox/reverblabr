# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Recordings on the disk: the writer thread that drains a Recorder block's
# tap, the files it writes through libsndfile, their names, and the repair
# of a file a crash cut off. Plain C++23 like the layers under it, so all of
# it runs in a test with no audio server and no Qt; the engine drives it.
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile>=1.1.0)
# A file at another rate than the session's.
pkg_check_modules(SOXR REQUIRED IMPORTED_TARGET soxr>=0.1.3)
# A recording's tags.
pkg_check_modules(TAGLIB REQUIRED IMPORTED_TARGET taglib>=2.0)
# M4A, AAC and ALAC: the distribution's FFmpeg, dynamically linked.
# Which encoders it has is asked at run time (probeEncoders), because a
# distribution may ship FFmpeg without some of them.
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat>=60 libavcodec>=60 libavutil>=58)

add_library(reverblabr-record STATIC
    audiofile.cpp
    audiofile.h
    audiofilereader.cpp
    audiofilereader.h
    filename.cpp
    filename.h
    impulsefile.cpp
    impulsefile.h
    m4afile.cpp
    m4afile.h
    recordingwriter.cpp
    recordingwriter.h
    repair.cpp
    repair.h
    resampler.cpp
    resampler.h
    summary.cpp
    summary.h
    tags.cpp
    tags.h
)
target_include_directories(reverblabr-record PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-record PUBLIC reverblabr-rt PRIVATE PkgConfig::SNDFILE PkgConfig::SOXR PkgConfig::TAGLIB PkgConfig::FFMPEG)
set_target_properties(reverblabr-record PROPERTIES AUTOMOC OFF AUTORCC OFF)

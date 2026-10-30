# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Streams to an internet radio server: the thread that drains a Broadcast
# block's tap, encodes it through the system's FFmpeg and sends it to
# Icecast or SHOUTcast over a socket of its own, plain or through TLS
# (ADR-0026). Plain C++23 like the recordings' layer it borrows the
# resampler from, so all of it runs in a test against a server the test
# makes, with no audio server and no Qt.
find_package(PkgConfig REQUIRED)
pkg_check_modules(STREAM_FFMPEG REQUIRED IMPORTED_TARGET libavformat>=60 libavcodec>=60 libavutil>=58)
# TLS for the stream: GnuTLS, which the distributions' FFmpeg already links
# for its own https and rtmps, so it adds nothing to what is installed.
pkg_check_modules(STREAM_GNUTLS REQUIRED IMPORTED_TARGET gnutls>=3.7)

add_library(reverblabr-stream STATIC
    backoff.cpp
    backoff.h
    broadcaststream.cpp
    broadcaststream.h
    connection.cpp
    connection.h
    outputstream.h
    protocol.cpp
    protocol.h
    streamencoder.cpp
    streamencoder.h
    text.h
    title.cpp
    title.h
)
target_include_directories(reverblabr-stream PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-stream
    PUBLIC reverblabr-rt
    PRIVATE reverblabr-record PkgConfig::STREAM_FFMPEG PkgConfig::STREAM_GNUTLS)
set_target_properties(reverblabr-stream PROPERTIES AUTOMOC OFF AUTORCC OFF)

# **A live stream** (ADR-0027): the same tap, with a picture drawn for it
# on the stream's thread and the two sent as H.264 and AAC in FLV over RTMP
# through FFmpeg. A library of its own, since only a build with the Live
# Stream block needs FreeType, HarfBuzz, fontconfig and libswscale; still
# plain C++23, so all of it runs in a test with no audio server and no Qt.
if(RB_BUILD_LIVESTREAM)
    add_library(reverblabr-livestream STATIC
        canvas.cpp
        canvas.h
        compositor.cpp
        compositor.h
        livestream.cpp
        livestream.h
        rtmpoutput.cpp
        rtmpoutput.h
        textpainter.cpp
        textpainter.h
        videoencoder.cpp
        videoencoder.h
        visuals.cpp
        visuals.h
    )
    target_include_directories(reverblabr-livestream PUBLIC ${PROJECT_SOURCE_DIR}/engine)
    target_link_libraries(reverblabr-livestream
        PUBLIC reverblabr-stream reverblabr-dsp
        PRIVATE reverblabr-record PkgConfig::STREAM_FFMPEG PkgConfig::LIVESTREAM_DRAWING)
    set_target_properties(reverblabr-livestream PROPERTIES AUTOMOC OFF AUTORCC OFF)
endif()

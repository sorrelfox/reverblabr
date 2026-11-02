# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# What a Model block and its host share (ADR-0031): the rings, what a folder
# holds and how a folder is read. Plain C++23 with no ONNX Runtime in it:
# only the host links that.
add_library(reverblabr-modelproto STATIC
    description.cpp
    description.h
    minijson.cpp
    minijson.h
    onnxreader.cpp
    onnxreader.h
    recognise.cpp
    recognise.h
    resampler.cpp
    resampler.h
    shared.cpp
    shared.h
)
target_include_directories(reverblabr-modelproto PUBLIC ${PROJECT_SOURCE_DIR}/engine)
set_target_properties(reverblabr-modelproto PROPERTIES AUTOMOC OFF AUTORCC OFF)

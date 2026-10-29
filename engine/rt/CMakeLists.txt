# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Real-time plumbing: the lock-free channels between the audio thread and the
# rest of the engine. Plain C++23, the lowest layer of all, so every layer
# above can use it. Header-only apart from draining the log, because each of
# these is a few instructions inside a process callback.
add_library(reverblabr-rt STATIC
    audiotap.h
    clock.h
    logger.cpp
    logger.h
    meter.h
    realtime.h
    retirequeue.h
    runwatch.cpp
    runwatch.h
    seqlock.h
    spscqueue.h
    triplebuffer.h
)
target_include_directories(reverblabr-rt PUBLIC ${PROJECT_SOURCE_DIR}/engine)
set_target_properties(reverblabr-rt PROPERTIES AUTOMOC OFF AUTORCC OFF)

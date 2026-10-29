# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# A Live Stream block's stream against a real RTMP server, MediaMTX, run in
# a container with podman: RTMP and RTMPS, read back by a player. **Not part
# of the default run**, as tests/broadcast is not: it needs podman and the
# network once to pull the image. `ctest --preset container` runs it; it
# skips (77) where podman cannot run a container. RB_LIVESTREAM_SOAK_SECONDS
# makes its long stream longer (1800 for half an hour).
if(NOT RB_BUILD_LIVESTREAM)
    return()
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(RB_LIVESTREAM_TEST_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

add_executable(reverblabr-mediamtx-test test_mediamtx.cpp)
target_include_directories(reverblabr-mediamtx-test PRIVATE ${PROJECT_SOURCE_DIR}/tests/unit/stream)
target_compile_definitions(reverblabr-mediamtx-test PRIVATE
    RB_MEDIAMTX_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/mediamtx.yml.in"
)
target_link_libraries(reverblabr-mediamtx-test PRIVATE
    reverblabr-livestream
    reverblabr-stream
    reverblabr-rt
    Qt6::Core
    GTest::gtest
    PkgConfig::RB_LIVESTREAM_TEST_FFMPEG
)
add_test(NAME livestream.mediamtx COMMAND reverblabr-mediamtx-test)
set_tests_properties(livestream.mediamtx PROPERTIES
    LABELS container
    SKIP_RETURN_CODE 77
    TIMEOUT 2400
)

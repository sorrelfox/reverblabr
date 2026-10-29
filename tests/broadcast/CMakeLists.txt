# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# A Broadcast block's stream against a real Icecast, Fedora's own, run in
# a container with podman. **Not part of the default run**: it needs
# podman, and the network once to build the image, and takes a minute. The
# test presets leave the `container` label out, and `ctest --preset
# container` runs it. It skips (77) when podman is missing or cannot run a
# container, which is the case inside CI's own container.
find_package(PkgConfig REQUIRED)
pkg_check_modules(RB_BROADCAST_TEST_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)

add_executable(reverblabr-icecast-test test_icecast.cpp)
target_include_directories(reverblabr-icecast-test PRIVATE ${PROJECT_SOURCE_DIR}/tests/unit/stream)
target_compile_definitions(reverblabr-icecast-test PRIVATE
    RB_ICECAST_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/icecast.xml.in"
    RB_ICECAST_IMAGE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(reverblabr-icecast-test PRIVATE
    reverblabr-stream
    reverblabr-rt
    Qt6::Core
    GTest::gtest
    PkgConfig::RB_BROADCAST_TEST_FFMPEG
)
add_test(NAME broadcast.icecast COMMAND reverblabr-icecast-test)
set_tests_properties(broadcast.icecast PROPERTIES
    LABELS container
    SKIP_RETURN_CODE 77
    TIMEOUT 900
)

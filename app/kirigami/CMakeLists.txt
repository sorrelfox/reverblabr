# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# **Kirigami's colours under the Reverblabr control style**. Kirigami
# loads a plugin whose file name starts with the style's name from
# `kf6/kirigami/platform` under each of the application's library paths.
# The application's own directory is one of them, so in the build tree the
# plugin sits beside the programs; installed, it goes where Kirigami's own
# platform plugins are.
add_library(Reverblabr MODULE reverblabrplatform.cpp)
target_include_directories(Reverblabr PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(Reverblabr PRIVATE Qt6::Core Qt6::Gui KF6::KirigamiPlatform)
set_target_properties(Reverblabr PROPERTIES
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/kf6/kirigami/platform"
)
install(TARGETS Reverblabr DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf6/kirigami/platform)

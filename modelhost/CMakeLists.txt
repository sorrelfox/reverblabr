# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# reverblabr-model-host (ADR-0031): one model in a process of its own. No Qt
# and no PipeWire: it reaches its block through shared memory alone. The only
# program that links ONNX Runtime.
add_executable(reverblabr-model-host main.cpp onnxrunner.cpp onnxrunner.h pipeline.cpp pipeline.h)
target_link_libraries(reverblabr-model-host PRIVATE reverblabr-version reverblabr-modelproto PkgConfig::ONNXRUNTIME)
set_target_properties(reverblabr-model-host PROPERTIES AUTOMOC OFF AUTORCC OFF)
install(TARGETS reverblabr-model-host DESTINATION ${KDE_INSTALL_LIBEXECDIR})

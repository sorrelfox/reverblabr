# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The engine, lowest layer first. A layer may use the layers listed before it,
# never those after it. The top-level CMakeLists.txt checks the rules.
add_subdirectory(rt)    # plain C++23: lock-free channels to the audio thread
add_subdirectory(dsp)   # plain C++23: DSP primitives and the block SDK
add_subdirectory(blocks) # plain C++23: the blocks a session is built from
add_subdirectory(graph) # plain C++23: graph compiler and real-time runtime
add_subdirectory(record) # plain C++23 and libsndfile: recordings on the disk
add_subdirectory(stream) # plain C++23 and FFmpeg: streams to an internet radio server
add_subdirectory(plugin) # plain C++23: what the engine and a plugin host share
add_subdirectory(pw)    # PipeWire client
add_subdirectory(mpris) # the media players on the session bus (QtDBus)
add_subdirectory(core)  # session model and engine facade (QtCore)
add_subdirectory(bus)   # the engine on the session bus (QtDBus)

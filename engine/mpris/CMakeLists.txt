# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The media players on the session bus (MPRIS): who is playing what, read
# and never driven, for a Broadcast block's title. QtCore and QtDBus only,
# like the engine, and built whether or not the Broadcast block is: the
# window lists the players from it too.
add_library(reverblabr-mpris STATIC
    player.cpp
    player.h
    playerwatcher.cpp
    playerwatcher.h
    trackfeed.cpp
    trackfeed.h
)
target_include_directories(reverblabr-mpris PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-mpris PUBLIC Qt6::Core Qt6::DBus)
set_target_properties(reverblabr-mpris PROPERTIES AUTOMOC ON AUTORCC OFF)

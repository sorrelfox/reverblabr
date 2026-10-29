# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Desktop notifications: which of the engine's errors, notices and
# finished recordings are worth telling a person about, and in what words.
# **Shared by the window and the daemon**, so both decide the same way: the
# window sends through KNotifications, the daemon, which has no KDE
# Frameworks (ADR-0017), straight to org.freedesktop.Notifications. The words
# come from the same `reverblabr` catalog as the window's, through gettext.
add_library(reverblabr-notify STATIC
    freedesktopsender.cpp
    freedesktopsender.h
    notifications.cpp
    notifications.h
    texts.cpp
    texts.h
)
target_include_directories(reverblabr-notify PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-notify PUBLIC Qt6::Core Qt6::DBus)
set_target_properties(reverblabr-notify PROPERTIES AUTOMOC ON)

# The events a person turns on and off in System Settings → Notifications,
# one per rb::notify::Event. KNotifications reads it for the window; the
# daemon reads its Action= lines itself (rb::notify::popupWanted()).
# Installed with the event names translated, which System Settings
# shows before Reverblabr has said a word.
rb_translate_data_file(reverblabr-notify DESKTOP
    ${CMAKE_CURRENT_SOURCE_DIR}/reverblabr.notifyrc ${CMAKE_CURRENT_BINARY_DIR}/reverblabr.notifyrc)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/reverblabr.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

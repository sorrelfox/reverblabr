#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Plays a sound through libcanberra with the properties that KNotification
# (KDE Frameworks) sets for a notification sound, so tests can see which
# stream properties an alert carries. canberra-gtk-play needs a display;
# this doesn't.
#
# Usage: play-notification.py SOUND_FILE [SECONDS]
# Exit status 77 means libcanberra isn't installed.

import ctypes
import sys
import time

try:
    canberra = ctypes.CDLL("libcanberra.so.0")
except OSError:
    sys.exit(77)

context = ctypes.c_void_p()
if canberra.ca_context_create(ctypes.byref(context)) != 0:
    sys.exit(1)


def text(value):
    return value.encode()


# KNotification sets these on the context: the application's display name,
# desktop file name and icon.
canberra.ca_context_change_props(context,
                                 text("application.name"), text("Test Notifier"),
                                 text("application.id"), text("org.example.notifier"),
                                 text("application.icon_name"), text("dialog-information"),
                                 None)
# ...and these per sound. It sets no media.role; libcanberra's PulseAudio
# backend adds "event".
result = canberra.ca_context_play(context, 0,
                                  text("event.id"), text("message-new-instant"),
                                  text("canberra.xdg-theme.name"), text("ocean"),
                                  text("media.filename"), text(sys.argv[1]),
                                  text("canberra.cache-control"), text("volatile"),
                                  None)
if result != 0:
    sys.exit(1)
time.sleep(float(sys.argv[2]) if len(sys.argv) > 2 else 3.0)

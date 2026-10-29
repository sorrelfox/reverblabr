#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Extracts the GUI's translatable strings into $podir/reverblabr.pot, and
# reverblabr-ctl's into $podir/reverblabr-ctl.pot.
#
# This follows KDE's Messages.sh convention: the caller sets $XGETTEXT (xgettext
# with the KI18n keywords) and $podir. Run tools/extract-messages.sh, or build
# the extract-messages target, instead of calling it directly. The GUI is
# scanned, and the desktop notifications' words (notify/), which the
# daemon says too, and the daemon's own (daemon/, what KRunner shows), and
# reverblabr-ctl's (ctl/) into a template of their own; the engine never
# produces user-visible text.

$XGETTEXT $(find app notify daemon -name '*.cpp' -o -name '*.h' -o -name '*.qml' | sort) -o "$podir/reverblabr.pot"
# reverblabr-ctl has a catalog of its own: it translates with plain gettext,
# through ctlTr(context, text) and ctlTrn(context, singular, plural, n).
$XGETTEXT -kctlTr:1c,2 -kctlTrn:1c,2,3 $(find ctl -name '*.cpp' -o -name '*.h' | sort) -o "$podir/reverblabr-ctl.pot"
# **The words the desktop shows before Reverblabr runs**: the desktop
# entry's name, comment, keywords and actions, the notification events System
# Settings lists, the runner KRunner's settings list, and the AppStream data a
# software centre shows. Not KI18n calls, so not $XGETTEXT's C++ parser:
# xgettext's own Desktop parser for the first three and its AppStream rules for
# the last, into a template of their
# own. cmake/RbTranslateDataFile.cmake merges the translations back into the
# files the build installs.
xgettext --from-code=UTF-8 --language=Desktop --package-name=reverblabr \
    --msgid-bugs-address=https://github.com/sorrelfox/reverblabr/issues \
    app/io.github.reverblabr.Reverblabr.desktop notify/reverblabr.notifyrc daemon/io.github.reverblabr.Runner.desktop \
    -o "$podir/reverblabr-data.pot"
xgettext --from-code=UTF-8 --join-existing app/io.github.reverblabr.Reverblabr.metainfo.xml -o "$podir/reverblabr-data.pot"

#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Writes the translation template reverblabr.pot, by running Messages.sh with
# xgettext set up for the KI18n calls used in C++ and QML.
#
# Usage: tools/extract-messages.sh [OUTPUT_DIR]   (default: po/)

set -euo pipefail

repo_root=$(cd "$(dirname "$0")/.." && pwd)
podir=$(realpath -m "${1:-$repo_root/po}")
mkdir -p "$podir"

# Every KI18n function that marks a string: plain, lazy (k...), markup (x...),
# with context (c: argument 1 is the context) and plurals (p).
keywords=()
for prefix in i18n ki18n kli18n xi18n kxi18n klxi18n; do
    keywords+=("-k${prefix}:1" "-k${prefix}c:1c,2" "-k${prefix}p:1,2" "-k${prefix}cp:1c,2,3")
done
keywords+=(-kI18N_NOOP:1 -kI18NC_NOOP:1c,2)
# The desktop notifications' words (notify/texts.h): marked like i18nc,
# and looked up with gettext where there is no KI18n.
keywords+=(-kRB_I18NC:1c,2)

# QML files are scanned with the C++ parser, as KDE's own tooling does; it
# finds KI18n.i18nc(...) calls the same way as i18nc(...) in C++.
XGETTEXT="xgettext --from-code=UTF-8 --language=C++ --add-comments=i18n --sort-by-file"
XGETTEXT+=" --package-name=reverblabr --copyright-holder=Reverblabr_contributors"
XGETTEXT+=" --msgid-bugs-address=https://github.com/sorrelfox/reverblabr/issues"
XGETTEXT+=" ${keywords[*]}"
export XGETTEXT podir

cd "$repo_root"
bash ./Messages.sh
# Every entry has one msgid line; the first one is the header.
for template in reverblabr reverblabr-ctl plasma_applet_io.github.reverblabr.sessions reverblabr-data; do
    messages=$(($(grep -c '^msgid ' "$podir/$template.pot") - 1))
    printf 'Wrote %s with %d messages.\n' "$podir/$template.pot" "$messages"
done

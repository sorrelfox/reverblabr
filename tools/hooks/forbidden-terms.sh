# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Shared by the pre-commit and commit-msg hooks; sourced, not executed.
#
# The clean-room term list is local to each clone: .git/info/forbidden-terms,
# one term per line, matched case-insensitively as a substring. "#" starts a
# comment. The file lives inside .git, so it is never committed or pushed.

# Writes the normalised term list (lower case, no comments or blank lines)
# to the file named by $1. Returns 1 when there is no list or it is empty.
rb_load_forbidden_terms() {
    local output=$1
    local list
    list="$(git rev-parse --git-common-dir)/info/forbidden-terms"
    [ -f "$list" ] || return 1
    sed -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' "$list" |
        grep -v '^$' | tr '[:upper:]' '[:lower:]' > "$output" || true
    [ -s "$output" ]
}

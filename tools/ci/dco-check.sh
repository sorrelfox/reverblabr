#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Every commit on the branch must carry a Signed-off-by trailer that matches
# its author, the Developer Certificate of Origin sign-off CONTRIBUTING.md
# asks for. Merge commits are exempt: they are made when a pull request is
# merged, not written by its author.
#
# Usage: dco-check.sh [BASE_REF]    (the commits after BASE_REF, by default
#                                    origin/main)

set -uo pipefail

base=${1:-origin/main}

if ! git rev-parse --verify --quiet "$base^{commit}" > /dev/null; then
    echo "dco: $base is unknown to this clone; fetch it or pass a base commit." >&2
    exit 1
fi

status=0
checked=0
while IFS= read -r commit; do
    [ -n "$commit" ] || continue
    if [ "$(git rev-list --no-walk --count --merges "$commit")" -eq 1 ]; then
        continue
    fi
    checked=$((checked + 1))
    author=$(git log -1 --format='%an <%ae>' "$commit")
    if ! git log -1 --format='%B' "$commit" | grep -qiF "Signed-off-by: $author"; then
        printf 'dco: %s (%s) has no "Signed-off-by: %s"\n' \
            "$(git log -1 --format=%h "$commit")" "$(git log -1 --format=%s "$commit")" "$author" >&2
        status=1
    fi
done < <(git rev-list "$base"..HEAD)

if [ "$status" -ne 0 ]; then
    echo 'dco: sign off with "git commit -s", or fix the trailers with "git rebase --signoff".' >&2
    exit 1
fi
echo "dco: $checked commits carry a matching sign-off."

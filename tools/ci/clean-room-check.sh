#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Clean-room check for CI: fails when a file name or an added line in this
# branch contains a term from the project's term list. It is the same rule the
# local pre-commit hook applies, for anyone who commits without the hook.
#
# The term list never lives in the repository. CI reads it from a secret,
# which keeps it out of the job's log:
#
#   RB_FORBIDDEN_TERMS_BASE64   base64 of the list, one term per line
#                               (printf '%s\n' term1 term2 | base64 -w0)
#   RB_FORBIDDEN_TERMS          a comma-separated list, as a fallback
#
# Without either the check says so and passes, so a fork or a new clone of
# the project isn't blocked by a list it doesn't have.
#
# The output never repeats the term itself: it names the file, the line and
# which term of the list matched.
#
# Usage: clean-room-check.sh [BASE_REF]    (the changes since BASE_REF, by
#                                          default origin/main)

set -uo pipefail

base=${1:-origin/main}

terms=$(mktemp)
trap 'rm -f "$terms"' EXIT

if [ -n "${RB_FORBIDDEN_TERMS_BASE64:-}" ]; then
    printf '%s' "$RB_FORBIDDEN_TERMS_BASE64" | base64 -d > "$terms" 2>/dev/null || {
        echo "clean-room: RB_FORBIDDEN_TERMS_BASE64 is not valid base64." >&2
        exit 1
    }
elif [ -n "${RB_FORBIDDEN_TERMS:-}" ]; then
    printf '%s' "$RB_FORBIDDEN_TERMS" | tr ',' '\n' > "$terms"
else
    echo "clean-room: no term list (RB_FORBIDDEN_TERMS_BASE64 or RB_FORBIDDEN_TERMS); nothing to check."
    echo "clean-room: add the list as a CI secret to turn this check on."
    exit 0
fi

# Normalise: drop comments, trim, lower-case, drop empty lines.
sed -i -e 's/#.*//' -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//' "$terms"
tr '[:upper:]' '[:lower:]' < "$terms" | grep -v '^$' > "$terms.clean" || true
mv "$terms.clean" "$terms"
if [ ! -s "$terms" ]; then
    echo "clean-room: the term list is empty; nothing to check."
    exit 0
fi
count=$(wc -l < "$terms")
echo "clean-room: checking the changes against $count terms, from $base."

if ! git rev-parse --verify --quiet "$base^{commit}" > /dev/null; then
    echo "clean-room: $base is unknown to this clone; fetch it or pass a base commit." >&2
    exit 1
fi

status=0

# File names added, copied, modified or renamed on this branch.
while IFS= read -r -d '' path; do
    lower=$(printf '%s' "$path" | tr '[:upper:]' '[:lower:]')
    number=0
    while IFS= read -r term; do
        number=$((number + 1))
        case "$lower" in
        *"$term"*)
            printf 'clean-room: file name matches term %d of the list: %s\n' "$number" "$path" >&2
            status=1
            ;;
        esac
    done < "$terms"
done < <(git diff --name-only -z --diff-filter=ACMR "$base"...HEAD)

# Lines the branch adds, reported as file:line without repeating the term.
# The top-level README is left out, as the pre-commit hook leaves it out: it
# names the program that inspired this one.
git diff --no-color --no-ext-diff -U0 --diff-filter=ACMR "$base"...HEAD -- . ':(top,exclude)README.md' |
    awk -v terms_file="$terms" '
        BEGIN {
            while ((getline term < terms_file) > 0) {
                if (term != "") {
                    terms[++count] = term
                }
            }
        }
        /^\+\+\+ b\// { file = substr($0, 7); next }
        /^@@ / {
            match($0, /\+[0-9]+/)
            line = substr($0, RSTART + 1, RLENGTH - 1) + 0
            next
        }
        /^\+/ {
            text = tolower(substr($0, 2))
            for (i = 1; i <= count; i++) {
                if (index(text, terms[i]) > 0) {
                    printf "clean-room: %s:%d matches term %d of the list\n", file, line, i > "/dev/stderr"
                    bad = 1
                }
            }
            line++
        }
        END { exit bad ? 1 : 0 }
    ' || status=1

if [ "$status" -ne 0 ]; then
    echo "clean-room: the branch mentions something the project never names; see the lines above." >&2
    exit 1
fi
echo "clean-room: clean."

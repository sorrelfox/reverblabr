#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Exercises tools/hooks/pre-commit and tools/hooks/commit-msg in a throwaway
# repository with a made-up term list.

set -euo pipefail

hooks_dir=$(cd "$(dirname "$0")/../../tools/hooks" && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    [ -f "$work_dir/stderr" ] && sed 's/^/  | /' "$work_dir/stderr" >&2
    exit 1
}

# commit_ok MESSAGE: the staged change must be accepted.
commit_ok() {
    git commit -q -m "$1" 2>"$work_dir/stderr" || fail "rejected: $1"
}

# commit_rejected MESSAGE EXPECTED: the commit must fail and stderr must
# contain EXPECTED.
commit_rejected() {
    if git commit -q -m "$1" 2>"$work_dir/stderr"; then
        fail "accepted: $1"
    fi
    grep -qF -- "$2" "$work_dir/stderr" || fail "no \"$2\" in the error output for: $1"
}

cd "$work_dir"
git init -q repo
cd repo
git config user.name "Hook Test"
git config user.email "hooks@example.invalid"
git config commit.gpgsign false
git config core.hooksPath "$hooks_dir"
cat > .git/info/forbidden-terms <<'LIST'
# made-up terms for the test
ForbiddenWord
  two words   # a comment after a term
LIST

echo "hello" > allowed.txt
git add allowed.txt
commit_ok "tests: an allowed change"

echo "this mentions FORBIDDENword in upper case" > content.txt
git add content.txt
commit_rejected "tests: forbidden content" "content.txt:1 contains the forbidden term \"forbiddenword\""
git rm -q --cached content.txt
rm content.txt

printf 'first line\nhere are Two Words together\n' > phrase.txt
git add phrase.txt
commit_rejected "tests: forbidden phrase" "phrase.txt:2"
git rm -q --cached phrase.txt
rm phrase.txt

# The top-level README may name what inspired the project; a README
# anywhere else is checked like any other file.
echo "inspired by ForbiddenWord" > README.md
git add README.md
commit_ok "tests: the top-level README is not checked"
mkdir -p docs
echo "inspired by ForbiddenWord" > docs/README.md
git add docs/README.md
commit_rejected "tests: another README is checked" "docs/README.md:1"
git rm -q --cached docs/README.md
rm -r docs

echo "harmless" > notes-forbiddenword.txt
git add notes-forbiddenword.txt
commit_rejected "tests: forbidden file name" "file name contains"
git rm -q --cached notes-forbiddenword.txt
rm notes-forbiddenword.txt

echo "more" >> allowed.txt
git add allowed.txt
commit_rejected "tests: mention ForbiddenWord in the message" "commit-msg: line 1"
git commit -q -F - <<'MESSAGE' 2>"$work_dir/stderr" || fail "a message whose only term is in a comment line was rejected"
tests: comment lines are not part of the message

# forbiddenword in a comment line, which git strips
MESSAGE

# Removing a line that contains a term is allowed: only added lines count.
echo "old forbiddenword line" > legacy.txt
git add legacy.txt
git -c core.hooksPath=/dev/null commit -q -m "tests: seed a legacy line"
echo "replacement" > legacy.txt
git add legacy.txt
commit_ok "tests: removing a line with a term is fine"

# Without a term list the hooks do nothing.
rm .git/info/forbidden-terms
echo "forbiddenword" > unchecked.txt
git add unchecked.txt
commit_ok "tests: no list, no check (forbiddenword)"

printf 'PASS: git hooks\n'

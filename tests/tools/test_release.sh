#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# tools/release.sh and tools/check-release-tag.sh, in a throwaway
# repository holding copies of VERSION, the AppStream data and the two
# scripts. Nothing here touches the real repository or pushes anywhere.

set -euo pipefail

tree=$(cd "$(dirname "$0")/../.." && pwd)
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
metainfo=app/io.github.reverblabr.Reverblabr.metainfo.xml

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    [ -f "$work_dir/stderr" ] && sed 's/^/  | /' "$work_dir/stderr" >&2
    exit 1
}

# refused WHAT EXPECTED -- COMMAND...: the command must fail, saying EXPECTED.
refused() {
    local what=$1 expected=$2
    shift 3
    if "$@" >/dev/null 2>"$work_dir/stderr"; then
        fail "accepted: $what"
    fi
    grep -qF -- "$expected" "$work_dir/stderr" || fail "no \"$expected\" when refusing: $what"
}

cd "$work_dir"
git init -q repo
cd repo
git config user.name "Release Test"
git config user.email "release@example.invalid"
git config commit.gpgsign false
git config tag.gpgsign false
mkdir -p app tools
cp "$tree/VERSION" VERSION
cp "$tree/$metainfo" "$metainfo"
cp "$tree/tools/release.sh" "$tree/tools/check-release-tag.sh" tools/
printf '0.1.0\n' >VERSION
git add -A
git commit -q -m "start"

# Refused before anything is written.
refused "a version that is not MAJOR.MINOR.PATCH" "MAJOR.MINOR.PATCH" -- tools/release.sh prepare 0.2
refused "the same version again" "already says 0.1.0" -- tools/release.sh prepare 0.1.0
refused "an older version" "not newer than 0.1.0" -- tools/release.sh prepare 0.0.9
echo change >>"$metainfo"
refused "a tree with changes" "the tree has changes" -- tools/release.sh prepare 0.2.0
git checkout -q -- "$metainfo"

# prepare writes VERSION and a release waiting for its notes.
tools/release.sh prepare 0.2.0 >/dev/null 2>"$work_dir/stderr" || fail "prepare 0.2.0 was refused"
[ "$(cat VERSION)" = "0.2.0" ] || fail "VERSION says $(cat VERSION) after prepare 0.2.0"
grep -q "<release version=\"0.2.0\" date=\"$(date +%F)\" type=\"stable\">" "$metainfo" \
    || fail "prepare added no dated 0.2.0 release"
[ "$(grep -m 1 -o '<release version="[^"]*"' "$metainfo")" = '<release version="0.2.0"' ] \
    || fail "the new release is not the newest"
grep -q '@NOTES@' "$metainfo" || fail "the new release has no @NOTES@ to fill in"

# nothing_made WHAT: a refused tag left neither a commit nor a tag behind.
nothing_made() {
    [ "$(git log -1 --format=%s)" = "start" ] || fail "$1 was refused, but made the commit \"$(git log -1 --format=%s)\""
    [ -z "$(git tag)" ] || fail "$1 was refused, but made the tag $(git tag)"
}

# tag refuses what is not ready, and makes nothing when it does.
refused "notes never written" "still say @NOTES@" -- tools/release.sh tag
nothing_made "notes never written"
sed -i 's/@NOTES@/Faster, and fewer clicks./' "$metainfo"
echo stray >stray.txt
git add stray.txt
refused "other files changed" "other files have changes" -- tools/release.sh tag
nothing_made "other files changed"
git rm -q --cached stray.txt
rm stray.txt

# tag commits the release and makes the tag, and pushes nothing.
tools/release.sh tag >/dev/null 2>"$work_dir/stderr" || fail "tag was refused on a ready release"
[ "$(git log -1 --format=%s)" = "release: 0.2.0" ] || fail "the release commit is \"$(git log -1 --format=%s)\""
git log -1 --format=%B | grep -q '^Signed-off-by: ' || fail "the release commit is not signed off"
[ "$(git describe --tags --exact-match HEAD)" = "v0.2.0" ] || fail "HEAD is not tagged v0.2.0"
[ "$(git cat-file -t v0.2.0)" = "tag" ] || fail "v0.2.0 is not an annotated tag"
[ -z "$(git status --porcelain)" ] || fail "tag left changes behind"
refused "the same tag twice" "exists already" -- tools/release.sh tag

# The tag check.
tools/check-release-tag.sh v0.2.0 >/dev/null 2>"$work_dir/stderr" || fail "check-release-tag refused v0.2.0"
refused "a tag that is not VERSION's" "VERSION says 0.2.0" -- tools/check-release-tag.sh v0.2.1
refused "a tag without its v" "VERSION says 0.2.0" -- tools/check-release-tag.sh 0.2.0
GITHUB_REF_NAME=v0.2.0 tools/check-release-tag.sh >/dev/null 2>"$work_dir/stderr" \
    || fail "check-release-tag refused GITHUB_REF_NAME=v0.2.0"
tools/check-release-tag.sh >/dev/null 2>"$work_dir/stderr" || fail "check-release-tag refused the tag on HEAD"

echo "PASS: release tools"

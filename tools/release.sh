#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Makes a release in two steps. The version is written in VERSION and
# nowhere else; this is what changes it.
#
#   tools/release.sh prepare X.Y.Z
#       Writes X.Y.Z to VERSION and adds a dated release to the AppStream
#       data, with @NOTES@ where its notes go. Needs a clean tree and a
#       version newer than the current one.
#
#   tools/release.sh tag
#       Checks the notes were written and that nothing but VERSION and the
#       AppStream data changed, commits them as "release: X.Y.Z" (signed
#       off) and makes the annotated tag vX.Y.Z. It never pushes: that comes
#       after the checks (docs/development.md, "Making a release").

set -euo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
metainfo=app/io.github.reverblabr.Reverblabr.metainfo.xml

die() {
    printf 'release: %s\n' "$*" >&2
    exit 1
}

current_version() {
    head -n 1 VERSION | tr -d '[:space:]'
}

newest_release() {
    grep -m 1 -o '<release version="[^"]*"' "$metainfo" | sed 's/.*"\(.*\)"/\1/'
}

prepare() {
    local version=$1
    [[ $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "a version is MAJOR.MINOR.PATCH, not \"$version\""
    local current
    current=$(current_version)
    [ "$version" != "$current" ] || die "VERSION already says $version"
    [ "$(printf '%s\n%s\n' "$current" "$version" | sort -V | tail -n 1)" = "$version" ] \
        || die "$version is not newer than $current"
    [ -z "$(git status --porcelain)" ] || die "the tree has changes; commit or put them away first"
    git rev-parse -q --verify "refs/tags/v$version" >/dev/null && die "the tag v$version exists already"

    printf '%s\n' "$version" >VERSION
    python3 - "$metainfo" "$version" "$(date +%F)" <<'EOF'
import sys
path, version, date = sys.argv[1:]
text = open(path, encoding="utf-8").read()
anchor = "  <releases>\n"
if text.count(anchor) != 1:
    sys.exit(f"release: no single '<releases>' in {path}")
entry = (f'    <release version="{version}" date="{date}" type="stable">\n'
         "      <description>\n"
         "        <p>@NOTES@</p>\n"
         "      </description>\n"
         "    </release>\n")
open(path, "w", encoding="utf-8").write(text.replace(anchor, anchor + entry))
EOF
    printf 'VERSION is %s. Now:\n' "$version"
    printf '  1. write the notes in %s, in place of @NOTES@;\n' "$metainfo"
    printf '  2. run the checks (docs/development.md, "Before a change goes in");\n'
    printf '  3. tools/release.sh tag\n'
}

tag() {
    local version
    version=$(current_version)
    [ "$(newest_release)" = "$version" ] \
        || die "the newest release in $metainfo is $(newest_release), VERSION says $version: run prepare first"
    ! grep -q '@NOTES@' "$metainfo" || die "the release notes still say @NOTES@"
    git rev-parse -q --verify "refs/tags/v$version" >/dev/null && die "the tag v$version exists already"

    local changed
    changed=$(git status --porcelain | awk '{print $2}' | grep -v -x -e VERSION -e "$metainfo" || true)
    [ -z "$changed" ] || die "other files have changes as well: $(echo "$changed" | tr '\n' ' ')"

    if [ -n "$(git status --porcelain)" ]; then
        git add VERSION "$metainfo"
        git commit -q -s -m "release: $version" -m "The version and its release notes (tools/release.sh)."
    fi
    git tag -a "v$version" -m "Reverblabr $version"
    "$root/tools/check-release-tag.sh" "v$version"
    printf 'Tagged v%s on %s. After the checks: move main to it and push both with the tag\n' \
        "$version" "$(git rev-parse --short HEAD)"
    printf '(docs/development.md, "Making a release").\n'
}

case "${1:-}" in
prepare)
    [ $# -eq 2 ] || die "usage: tools/release.sh prepare X.Y.Z"
    prepare "$2"
    ;;
tag)
    [ $# -eq 1 ] || die "usage: tools/release.sh tag"
    tag
    ;;
*)
    die "usage: tools/release.sh prepare X.Y.Z | tag"
    ;;
esac

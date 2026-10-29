#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# A release tag has to be the version the tree says it is: v + the
# first line of VERSION, with that release the newest in the AppStream data
# and its notes written. A tag that is anything else fails, so a mistyped
# tag cannot publish a release under the wrong number.
#
#   tools/check-release-tag.sh [TAG]
#
# TAG defaults to $GITHUB_REF_NAME (what a tag push hands a GitHub Actions
# job), then to the tag on HEAD.

set -euo pipefail

root=$(git rev-parse --show-toplevel)
cd "$root"
metainfo=app/io.github.reverblabr.Reverblabr.metainfo.xml

tag=${1:-${GITHUB_REF_NAME:-}}
if [ -z "$tag" ]; then
    tag=$(git describe --tags --exact-match HEAD 2>/dev/null) || {
        echo "check-release-tag: no tag given and none on HEAD" >&2
        exit 1
    }
fi

version=$(head -n 1 VERSION | tr -d '[:space:]')
newest=$(grep -m 1 -o '<release version="[^"]*"' "$metainfo" | sed 's/.*"\(.*\)"/\1/')

fail() {
    printf 'check-release-tag: %s\n' "$*" >&2
    exit 1
}

[ "$tag" = "v$version" ] || fail "the tag is $tag, but VERSION says $version (the tag would be v$version)"
[ "$newest" = "$version" ] || fail "the newest release in $metainfo is $newest, not $version"
! grep -q '@NOTES@' "$metainfo" || fail "the release notes still say @NOTES@"
printf 'check-release-tag: %s is the version the tree says it is\n' "$tag"

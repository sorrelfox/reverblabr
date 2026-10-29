#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Makes the source RPM from a release tarball:
#
#   packaging/rpm/srpm.sh dist/reverblabr-X.Y.Z.tar.xz [OUTDIR]
#
# The spec is the tarball's own packaging/rpm/reverblabr.spec.in, so the
# package is always built the way that release says. Its @VERSION@ comes
# from the tarball's name, and the changelog's date from the release's entry
# in the tarball's AppStream data, so the same tarball always makes the same
# spec. OUTDIR (default: the tarball's folder) gets reverblabr.spec and the
# .src.rpm, which is what COPR builds and what rpmbuild --rebuild takes.

set -euo pipefail

die() {
    printf 'srpm: %s\n' "$*" >&2
    exit 1
}

[ $# -ge 1 ] && [ $# -le 2 ] || die "usage: packaging/rpm/srpm.sh reverblabr-X.Y.Z.tar.xz [OUTDIR]"
tarball=$(realpath -- "$1")
outdir=$(realpath -- "${2:-$(dirname -- "$tarball")}")
[ -f "$tarball" ] || die "no tarball at $1"
command -v rpmbuild >/dev/null || die "rpmbuild is missing (dnf install rpm-build)"

name=$(basename -- "$tarball")
[[ $name =~ ^reverblabr-([0-9]+\.[0-9]+\.[0-9]+)\.tar\.xz$ ]] \
    || die "a release tarball is reverblabr-X.Y.Z.tar.xz, not $name"
version=${BASH_REMATCH[1]}
top=reverblabr-$version

# The newest release in the AppStream data is the version's: tools/release.sh
# writes it, and tools/check-release-tag.sh holds a tag to it.
date=$(tar -xOJf "$tarball" "$top/app/io.github.reverblabr.Reverblabr.metainfo.xml" \
    | grep -m 1 -o '<release version="[^"]*" date="[^"]*"' \
    | sed -n "s/^<release version=\"$version\" date=\"\(.*\)\"$/\1/p")
[ -n "$date" ] || die "the newest release in the tarball's AppStream data is not $version"
changelog_date=$(LC_ALL=C date -u -d "$date" '+%a %b %d %Y')

mkdir -p -- "$outdir"
tar -xOJf "$tarball" "$top/packaging/rpm/reverblabr.spec.in" \
    | sed -e "s/@VERSION@/$version/g" \
          -e "s/@CHANGELOG_DATE@/$changelog_date/g" \
          -e "s/@CHANGELOG_EMAIL@/${RB_PACKAGER_EMAIL:-348316453+sorrelfox@users.noreply.github.com}/g" \
          >"$outdir/reverblabr.spec"

# SOURCE_DATE_EPOCH from the changelog makes the source RPM's own timestamps
# the release's, not the moment it was made.
rpmbuild -bs "$outdir/reverblabr.spec" \
    --define "_sourcedir $(dirname -- "$tarball")" \
    --define "_srcrpmdir $outdir" \
    --define "use_source_date_epoch_as_buildtime 1" \
    --define "source_date_epoch_from_changelog 1" \
    --define "clamp_mtime_to_source_date_epoch 1"

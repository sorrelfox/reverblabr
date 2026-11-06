#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Builds the Debian source and binary packages from a release tarball, in a
# plain Debian testing or Ubuntu 26.04 system:
#
#   packaging/debian/build-deb.sh dist/reverblabr-X.Y.Z.tar.xz [OUTDIR]
#
# The packaging is the tarball's own packaging/debian, so a package is always
# built the way its release says. The version comes from the tarball's name
# and the changelog's date from the release's entry in the tarball's
# AppStream data, as packaging/rpm/srpm.sh does. On Ubuntu the version gets
# "~ubuntuNN.NN", so the two distributions' packages have different names
# and Debian's is the newer.
#
# A block whose library the distribution does not package (at the version
# the block needs) is left out through its build profile. As root, the
# script installs the build tools and dependencies with apt; otherwise they
# must be there. Then lintian checks the packages and fails on an error.
# OUTDIR (default: the tarball's folder) gets the .deb, the source package
# and the .changes file.

set -euo pipefail

die() {
    printf 'build-deb: %s\n' "$*" >&2
    exit 1
}

[ $# -ge 1 ] && [ $# -le 2 ] || die "usage: packaging/debian/build-deb.sh reverblabr-X.Y.Z.tar.xz [OUTDIR]"
tarball=$(realpath -- "$1")
[ -f "$tarball" ] || die "no tarball at $1"
outdir=${2:-$(dirname -- "$tarball")}
mkdir -p -- "$outdir"
outdir=$(realpath -- "$outdir")

name=$(basename -- "$tarball")
[[ $name =~ ^reverblabr-([0-9]+\.[0-9]+\.[0-9]+)\.tar\.xz$ ]] \
    || die "a release tarball is reverblabr-X.Y.Z.tar.xz, not $name"
version=${BASH_REMATCH[1]}
top=reverblabr-$version

export DEBIAN_FRONTEND=noninteractive
as_root=false
[ "$(id -u)" -eq 0 ] && as_root=true
if $as_root; then
    apt-get update -qq
    apt-get install -y -qq --no-install-recommends dpkg-dev lintian xz-utils >/dev/null
fi

. /etc/os-release
case "$ID" in
    debian)
        suffix=
        distribution=unstable
        ;;
    ubuntu)
        suffix="~ubuntu$VERSION_ID"
        distribution=$VERSION_CODENAME
        ;;
    *) die "this is $ID; the packaging is for Debian and Ubuntu" ;;
esac

# The newest release in the AppStream data is the version's: tools/release.sh
# writes it, and tools/check-release-tag.sh holds a tag to it. grep reads to
# the end: one that stopped at the first match could leave tar writing into a
# closed pipe, and with pipefail that ends the script (SIGPIPE, 141).
date=$(tar -xOJf "$tarball" "$top/app/io.github.reverblabr.Reverblabr.metainfo.xml" \
    | grep -o '<release version="[^"]*" date="[^"]*"' \
    | sed -n "1s/^<release version=\"$version\" date=\"\(.*\)\"$/\1/p")
[ -n "$date" ] || die "the newest release in the tarball's AppStream data is not $version"
changelog_date=$(LC_ALL=C date -u -R -d "$date 12:00")

# The profiles: a library this system has no candidate for, or only an older
# one, leaves its block out.
profiles=()
lacks() {
    local candidate
    candidate=$(apt-cache policy "$1" 2>/dev/null | sed -n 's/^ *Candidate: //p')
    [ -z "$candidate" ] || [ "$candidate" = "(none)" ] && return 0
    ! dpkg --compare-versions "$candidate" ge "$2"
}
lacks librnnoise-dev 0.2 && profiles+=(pkg.reverblabr.nornnoise)
lacks libonnxruntime-dev 1.20 && profiles+=(pkg.reverblabr.nomodel)
lacks libvst3sdk-dev 3.8 && profiles+=(pkg.reverblabr.novst3)
profile_list=$(IFS=,; echo "${profiles[*]}")
echo "Building reverblabr $version-1$suffix for $PRETTY_NAME, profiles: ${profile_list:-none}"

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
cp -- "$tarball" "$work/reverblabr_$version.orig.tar.xz"
tar -xJf "$tarball" -C "$work"
src=$work/$top
cp -a -- "$src/packaging/debian" "$src/debian"
rm -f -- "$src/debian/build-deb.sh" "$src/debian/install-check.sh" "$src/debian/changelog.in"
sed -e "s/@VERSION@/$version/g" -e "s/@SUFFIX@/$suffix/g" \
    -e "s/@DISTRIBUTION@/$distribution/g" -e "s/@DATE@/$changelog_date/g" \
    "$src/packaging/debian/changelog.in" >"$src/debian/changelog"

if $as_root; then
    apt-get build-dep -y -qq ${profile_list:+-P "$profile_list"} "$src" >"$work/build-dep.log" 2>&1 \
        || { tail -40 "$work/build-dep.log"; die "apt could not install the build dependencies"; }
fi

export DEB_BUILD_OPTIONS=${DEB_BUILD_OPTIONS:-parallel=$(nproc)}
(cd "$src" && dpkg-buildpackage -us -uc ${profile_list:+-P"$profile_list"}) >"$work/build.log" 2>&1 \
    || { cp -- "$work/build.log" "$outdir/build-$ID.log"; grep -m 20 -E 'CMake Error|error:|FAILED' "$work/build.log"; die "dpkg-buildpackage failed; its log is $outdir/build-$ID.log"; }
grep -E '^(The following tests FAILED|[0-9]+% tests passed)' "$work/build.log" || true

changes=$work/reverblabr_$version-1${suffix}_$(dpkg --print-architecture).changes
lintian --fail-on error --display-info "$changes" || die "lintian found errors"

cp -- "$work"/*.deb "$work"/*.dsc "$work"/*.debian.tar.xz "$work"/*.buildinfo "$changes" "$outdir/"
[ -e "$outdir/reverblabr_$version.orig.tar.xz" ] || cp -- "$work/reverblabr_$version.orig.tar.xz" "$outdir/"
cp -- "$work/build.log" "$outdir/build-$ID.log"
ls -l "$outdir"

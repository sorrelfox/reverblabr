#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# Builds the Flatpak (io.github.reverblabr.Reverblabr.yml) into a repository of
# its own and makes a bundle of it.
#
#   packaging/flatpak/build-flatpak.sh [TARBALL] [OUT_DIR]
#
# With a release tarball (reverblabr-X.Y.Z.tar.xz) the app is built from it, as
# the release does; without one, from this tree. OUT_DIR (default
# build/flatpak) gets reverblabr-X.Y.Z-x86_64.flatpak, the repository and the
# build's log. flatpak-builder installs the KDE SDK and runtime for the user
# from Flathub when they are missing; the dependencies' sources are downloaded
# and checked against the manifest's sha256 sums.
#
# smoke-flatpak.sh then checks the bundle as it is installed.
set -euo pipefail

here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
id=io.github.reverblabr.Reverblabr
tarball=${1:-}
out=$(mkdir -p "${2:-$root/build/flatpak}" && cd "${2:-$root/build/flatpak}" && pwd)

if [ -n "$tarball" ]; then
    tarball=$(cd "$(dirname "$tarball")" && pwd)/$(basename "$tarball")
    version=$(basename "$tarball" | sed -n 's/^reverblabr-\(.*\)\.tar\.[a-z0-9]*$/\1/p')
    [ -n "$version" ] || { echo "not a release tarball: $tarball" >&2; exit 1; }
else
    version=$(tr -d '[:space:]' < "$root/VERSION")
fi

# The manifest builds the tree it is in (type: dir, path: ../..), and that
# source is the last thing in it. A copy next to the output names the tree, or
# the tarball, by its full path instead.
manifest=$out/$id.yml
sed '/^      - type: dir$/,$d' "$here/$id.yml" > "$manifest"
if [ -n "$tarball" ]; then
    printf '      - type: archive\n        path: %s\n' "$tarball" >> "$manifest"
else
    printf '      - type: dir\n        path: %s\n        skip: [build, .flatpak-builder]\n' "$root" >> "$manifest"
fi

# --disable-cache: a local tree or tarball is not part of flatpak-builder's
# cache key, so a changed one would be skipped. The dependencies are quick.
# RB_FLATPAK_JOBS caps the compilers run at once (flatpak-builder runs one
# per CPU it sees): each takes up to 600 MB, which matters on a machine with
# little memory for its CPUs, such as a CI runner.
echo "building $id $version (log: $out/build.log)"
flatpak-builder --user --install-deps-from=flathub --force-clean --disable-cache \
    ${RB_FLATPAK_JOBS:+--jobs="$RB_FLATPAK_JOBS"} \
    --state-dir="$out/state" --repo="$out/repo" --default-branch=stable \
    "$out/build" "$manifest" > "$out/build.log" 2>&1 || { tail -60 "$out/build.log"; exit 1; }

bundle=$out/reverblabr-$version-x86_64.flatpak
flatpak build-bundle --runtime-repo=https://dl.flathub.org/repo/flathub.flatpakrepo \
    "$out/repo" "$bundle" "$id" stable

ls -l "$bundle"

#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Proves that a clean distribution image can build Reverblabr using nothing but
# the package list in tools/deps/. The committed tree (git archive HEAD) is
# copied into a throwaway container, so nothing from the host leaks in.
#
# Usage: tools/container-check.sh [IMAGE] [STAGE]
#   IMAGE  fedora:44 (default) or ubuntu:26.04
#   STAGE  deps       install the packages and run tools/check-deps.sh
#          configure  as deps, then configure the debug preset (default)
#          test       as configure, then build and run the tests
#
# Uses podman; set CONTAINER_ENGINE=docker to use Docker instead.

set -euo pipefail

usage() {
    sed -n '/^# Usage:/,/^# Uses podman/p' "$0" | sed 's/^# \{0,1\}//'
}

case "${1:-}" in
    -h | --help) usage; exit 0 ;;
esac

image=${1:-fedora:44}
stage=${2:-configure}
engine=${CONTAINER_ENGINE:-podman}

case "$stage" in
    deps | configure | test) ;;
    *) usage >&2; exit 2 ;;
esac

case "$image" in
    fedora:*)
        image="registry.fedoraproject.org/$image"
        ;;
    ubuntu:*)
        image="docker.io/library/$image"
        ;;
esac

case "$image" in
    */fedora:*)
        package_list=tools/deps/fedora.txt
        install_command='dnf -y --setopt=install_weak_deps=False install'
        ;;
    */ubuntu:*)
        package_list=tools/deps/kubuntu.txt
        install_command='apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get -y -qq install --no-install-recommends'
        ;;
    *)
        printf 'Unsupported image: %s (use fedora:NN or ubuntu:NN.NN)\n' "$image" >&2
        exit 2
        ;;
esac

repo_root=$(git rev-parse --show-toplevel)

# Runs inside the container. The source tree arrives as a tar stream on stdin.
container_script=$(cat <<EOF
set -euo pipefail
mkdir /src
tar -x -C /src
cd /src
echo "== Installing \$(sed 's/#.*//' $package_list | wc -w) packages from $package_list"
$install_command \$(sed 's/#.*//' $package_list)
echo "== tools/check-deps.sh"
tools/check-deps.sh
if [ "$stage" = deps ]; then
    exit 0
fi
if [ ! -f CMakePresets.json ]; then
    echo "== No CMakePresets.json in this revision; nothing to configure."
    exit 0
fi
echo "== Configure (debug preset)"
cmake --preset debug
if [ "$stage" = test ]; then
    echo "== Build and test (debug preset)"
    cmake --build --preset debug
    ctest --preset debug
fi
EOF
)

printf 'Checking %s at %s in %s (stage: %s)\n' \
    "$(git -C "$repo_root" rev-parse --abbrev-ref HEAD)" \
    "$(git -C "$repo_root" rev-parse --short HEAD)" "$image" "$stage"
git -C "$repo_root" archive --format=tar HEAD |
    "$engine" run --rm -i "$image" bash -c "$container_script"

#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Checks the AppImage on a system that has none of what it brings: every
# program's --version, the AppImage called by another program's name, and the
# window for a few seconds with the engine it starts itself (ADR-0037).
#
#   packaging/appimage/smoke-appimage.sh dist/reverblabr-X.Y.Z-x86_64.AppImage
#
# Meant for a plain container of an older distribution than the one it was
# built on, such as ubuntu:24.04, whose glibc is older than Fedora 44's. As
# root it installs only what the AppImage leaves to the system, with apt or
# dnf: the graphics libraries, PipeWire's client library and its
# configuration, D-Bus, and a font with fontconfig's configuration. A
# container has no FUSE, so the AppImage is unpacked (--appimage-extract)
# and run from there, as on a system without FUSE.
#
# **Never against the desktop's audio or bus**: the window and its engine run
# offscreen on a session bus of their own (dbus-run-session), with no
# PipeWire socket (XDG_RUNTIME_DIR is a folder of their own), and with a home
# of their own. The processes it starts are stopped by their pids, never by
# a name.

set -euo pipefail

die() {
    printf 'smoke-appimage: %s\n' "$*" >&2
    exit 1
}

[ $# -eq 1 ] || die "usage: packaging/appimage/smoke-appimage.sh reverblabr-X.Y.Z-x86_64.AppImage"
appimage=$(realpath -- "$1")
[[ $(basename -- "$appimage") =~ ^reverblabr-([0-9]+\.[0-9]+\.[0-9]+)-x86_64\.AppImage$ ]] \
    || die "not the AppImage's name: $1"
version=${BASH_REMATCH[1]}
seconds=${RB_SMOKE_SECONDS:-15}

if [ "$(id -u)" -eq 0 ]; then
    if command -v apt-get > /dev/null; then
        export DEBIAN_FRONTEND=noninteractive
        { apt-get update -qq && apt-get install -y -qq --no-install-recommends \
            libegl1 libgl1 libopengl0 dbus-daemon dbus-bin fonts-dejavu-core fontconfig-config \
            "$(apt-cache show libpipewire-0.3-0t64 > /dev/null 2>&1 && echo libpipewire-0.3-0t64 || echo libpipewire-0.3-0)" \
            libpipewire-0.3-common; } \
            > /tmp/smoke-apt.log 2>&1 || { tail -n 30 /tmp/smoke-apt.log >&2; die "apt failed"; }
    elif command -v dnf > /dev/null; then
        dnf -y install mesa-libEGL mesa-libGL libglvnd-opengl dbus-daemon dbus-tools dejavu-sans-fonts fontconfig pipewire-libs \
            > /tmp/smoke-dnf.log 2>&1 || { tail -n 30 /tmp/smoke-dnf.log >&2; die "dnf failed"; }
    fi
fi
. /etc/os-release
echo "on $PRETTY_NAME, glibc $(getconf GNU_LIBC_VERSION | cut -d' ' -f2)"

work=$(mktemp -d)
cleanup() {
    rm -rf -- "$work"
}
trap cleanup EXIT
(cd "$work" && "$appimage" --appimage-extract > /dev/null) || die "the AppImage did not unpack"
root=$work/squashfs-root
# A home and a runtime folder of their own: no PipeWire socket, no settings
# of the person's, and nothing left behind.
export HOME=$work/home XDG_RUNTIME_DIR=$work/run
mkdir -p "$HOME" "$XDG_RUNTIME_DIR"
chmod 0700 "$XDG_RUNTIME_DIR"
unset XDG_CONFIG_HOME XDG_DATA_HOME XDG_STATE_HOME XDG_CACHE_HOME
export QT_QPA_PLATFORM=offscreen

failed=0
says() {
    local what=$1 answer
    shift
    answer=$("$@" --version 2> "$work/stderr" | head -n 1) || true
    echo "$what: $answer"
    [ "${answer##* }" = "$version" ] || {
        sed 's/^/  /' "$work/stderr" | head -n 20 >&2
        echo "  expected $version" >&2
        failed=1
    }
}
for program in reverblabr reverblabrd reverblabr-ctl reverblabr-plugin-host reverblabr-model-host; do
    says "$program" "$root/usr/bin/$program"
done
# The AppImage called by another program's name.
ln -s "$root/AppRun" "$work/reverblabr-ctl"
says "AppRun as reverblabr-ctl" "$work/reverblabr-ctl"
says "AppRun" "$root/AppRun"

# The window, which starts reverblabrd from the AppImage and takes the engine
# from it, and reverblabr-ctl, which reaches that engine.
busconf=$work/bus.conf
cat > "$busconf" <<'EOF'
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <type>session</type>
  <listen>unix:tmpdir=/tmp</listen>
  <policy context="default">
    <allow send_destination="*" eavesdrop="true"/>
    <allow eavesdrop="true"/>
    <allow own="*"/>
  </policy>
</busconfig>
EOF
export root seconds work
dbus-run-session --config-file="$busconf" -- bash -c '
    engine_pid() {
        dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
            org.freedesktop.DBus.GetConnectionUnixProcessID string:io.github.reverblabr.Engine 2> /dev/null \
            | sed -n "s/^ *uint32 //p"
    }
    "$root/AppRun" > "$work/window.log" 2>&1 &
    window=$!
    daemon=
    for _ in $(seq $((seconds * 4))); do
        daemon=$(engine_pid)
        [ -n "$daemon" ] && break
        sleep 0.25
    done
    sleep 2
    ok=yes
    if [ -z "$daemon" ]; then
        echo "window: no engine took its name" >&2
        ok=no
    else
        name=$(cat "/proc/$daemon/comm")
        echo "window: the engine took its name: pid $daemon, shown as $name"
        [ "$name" = reverblabrd ] || { echo "  expected reverblabrd" >&2; ok=no; }
        tr "\0" " " < "/proc/$daemon/cmdline" | grep -q "$root/usr/lib/reverblabr-appimage/programs/reverblabrd" \
            || { echo "  not the AppImage'\''s engine: $(tr "\0" " " < "/proc/$daemon/cmdline")" >&2; ok=no; }
        if "$root/usr/bin/reverblabr-ctl" --no-activate status > "$work/ctl.log" 2>&1; then
            echo "reverblabr-ctl status: reached it"
        else
            echo "reverblabr-ctl status failed:" >&2
            sed "s/^/  /" "$work/ctl.log" >&2
            ok=no
        fi
    fi
    kill -0 "$window" 2> /dev/null || { echo "window: gone before it was closed" >&2; ok=no; }
    if grep -qi "can.t be reached\|running the engine in this window" "$work/window.log"; then
        echo "window: ran an engine of its own" >&2
        ok=no
    fi
    kill "$window" 2> /dev/null || true
    [ -n "$daemon" ] && kill "$daemon" 2> /dev/null || true
    wait "$window" 2> /dev/null || true
    for _ in $(seq 20); do
        [ -n "$daemon" ] && kill -0 "$daemon" 2> /dev/null || break
        sleep 0.25
    done
    if [ "$ok" = no ]; then
        echo "the window said:" >&2
        sed "s/^/  /" "$work/window.log" | head -n 40 >&2
    fi
    [ "$ok" = yes ]
' || failed=1

exit "$failed"

#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# Checks a Flatpak bundle as people run it: installed for the user, every
# program's --version, and the window for a few seconds with the engine it
# starts itself (ADR-0034).
#
#   packaging/flatpak/smoke-flatpak.sh dist/reverblabr-X.Y.Z-x86_64.flatpak
#
# **Never against the desktop's audio or bus**: the sandbox is run without the
# PipeWire socket and without the host's PipeWire configuration, so its engine
# can neither reach nor rewrite the person's virtual devices, and the window
# runs on a session bus of its own (dbus-run-session), so the engine's name,
# the tray and the global shortcuts never reach the desktop's.
#
# The runtime must be installed already (build-flatpak.sh does that). An
# install that was there before stays; one made here is removed again, with
# its data.
set -euo pipefail

bundle=${1:?usage: smoke-flatpak.sh BUNDLE}
id=io.github.reverblabr.Reverblabr
version=$(basename "$bundle" | sed -n 's/^reverblabr-\(.*\)-[a-z0-9_]*\.flatpak$/\1/p')
[ -n "$version" ] || { echo "not a bundle's name: $bundle" >&2; exit 1; }
seconds=${RB_SMOKE_SECONDS:-8}
isolated=(--nofilesystem=xdg-run/pipewire-0 --nofilesystem=xdg-config/pipewire --env=QT_QPA_PLATFORM=offscreen)

if flatpak ps --columns=application | grep -qx "$id"; then
    echo "$id is running; close it first" >&2
    exit 1
fi
had=$(flatpak info --user "$id" > /dev/null 2>&1 && echo yes || echo no)
# --no-deps: the bundle names Flathub for its runtime, which is in place.
flatpak install --user --noninteractive -y --reinstall --no-deps "$bundle" > /dev/null
cleanup() {
    flatpak kill "$id" > /dev/null 2>&1 || true
    if [ "$had" = no ]; then
        flatpak uninstall --user --noninteractive -y --delete-data "$id" > /dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

failed=0
# KDE puts the libexec folder under lib for a prefix other than /usr.
for program in reverblabr reverblabrd reverblabr-ctl /app/lib/libexec/reverblabr-plugin-host; do
    answer=$(flatpak run "${isolated[@]}" --command="$program" "$id" --version 2> /dev/null | head -n 1)
    echo "$program: $answer"
    [ "${answer##* }" = "$version" ] || { echo "  expected $version" >&2; failed=1; }
done

# The window, which starts reverblabrd in its sandbox and takes the engine
# from it. dbus-run-session ends when the script does; the sandbox's processes
# are stopped first, by the app's id, never by a pid.
export id seconds
export isolated_args="${isolated[*]}"
# The bus has no activation files: nothing of the desktop's (portals, KWallet,
# gvfs) is started on it.
busconf=$(mktemp)
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
dbus-run-session --config-file="$busconf" -- bash -c '
    owned() {
        dbus-send --session --print-reply --dest=org.freedesktop.DBus /org/freedesktop/DBus \
            org.freedesktop.DBus.NameHasOwner string:io.github.reverblabr.Engine 2> /dev/null | grep -q "boolean true"
    }
    # shellcheck disable=SC2086
    log=$(mktemp)
    flatpak run $isolated_args "$id" > "$log" 2>&1 &
    reached=no
    for _ in $(seq $((seconds * 4))); do
        if owned; then reached=yes; break; fi
        sleep 0.25
    done
    sleep 2
    alive=$(flatpak ps --columns=application | grep -cx "$id" || true)
    flatpak kill "$id" > /dev/null 2>&1 || true
    sleep 1
    echo "window: the engine took its name: $reached; sandboxes running: $alive"
    grep -i "reverblabr\.\|error\|fatal" "$log" | sort -u | head -20 || true
    rm -f "$log"
    [ "$reached" = yes ] && [ "$alive" -ge 1 ]
' || failed=1
rm -f "$busconf"

exit "$failed"

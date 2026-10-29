#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Runs a command against a private PipeWire instance that can't touch the
# user's real audio session. It starts pipewire, wireplumber and (unless
# --no-pulse) pipewire-pulse with:
# - their own runtime directory, at a short path under /tmp, because Unix
#   socket paths are limited to 108 bytes
# - their own XDG_CONFIG_HOME and XDG_STATE_HOME, so the user's configuration
#   and saved state neither apply nor change
# - the distribution's pipewire.conf plus the test devices in pipewire.conf.d/,
#   but none of the system's drop-ins (one of them may enable discovery of
#   network speakers)
# - WirePlumber's policy-only profile from wireplumber.conf.d/: no hardware
#   monitors, so no sound card or Bluetooth device is ever opened
# - a private D-Bus session bus
#
# The command runs with PipeWire and Pulse clients pointed at that instance,
# and with these variables set:
#   RB_PW_HARNESS=1           marks the private instance; the tests refuse to
#                             run without it
#   RB_PW_HARNESS_DIR         scratch directory of this run (logs, test files)
#   RB_PW_HARNESS_PULSE       1 when pipewire-pulse is running
#   RB_HOST_XDG_RUNTIME_DIR   the caller's runtime directory, so tests can
#                             check that nothing leaks into the real session
# When the command fails, the daemons' logs are printed.
#
# Usage: tests/pw/harness/run-private-pipewire.sh [--no-pulse] COMMAND [ARGS...]

set -euo pipefail

harness=$(cd "$(dirname "$0")" && pwd)

if [ "${1:-}" != --inside ]; then
    with_pulse=1
    if [ "${1:-}" = --no-pulse ]; then
        with_pulse=0
        shift
    fi
    if [ $# -eq 0 ]; then
        sed -n 's/^# Usage: //p' "$0" >&2
        exit 2
    fi

    tools=(pipewire wireplumber pw-dump pw-metadata pw-cat dbus-run-session)
    [ "$with_pulse" = 0 ] || tools+=(pipewire-pulse paplay pactl)
    for tool in "${tools[@]}"; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            echo "run-private-pipewire: $tool is not installed; see tools/deps/" >&2
            exit 2
        fi
    done
    system_conf=/usr/share/pipewire/pipewire.conf
    if [ ! -f "$system_conf" ]; then
        echo "run-private-pipewire: $system_conf not found" >&2
        exit 2
    fi

    base=$(mktemp -d /tmp/rbpw.XXXXXX)
    trap 'rm -rf "$base"' EXIT
    mkdir -p "$base/run" "$base/logs" "$base/state" \
        "$base/pipewire/pipewire.conf.d" "$base/config/wireplumber/wireplumber.conf.d"
    chmod 700 "$base/run"
    cp "$system_conf" "$base/pipewire/"
    cp "$harness"/pipewire.conf.d/*.conf "$base/pipewire/pipewire.conf.d/"
    cp "$harness"/wireplumber.conf.d/*.conf "$base/config/wireplumber/wireplumber.conf.d/"

    export RB_HOST_XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-}"
    export RB_PW_HARNESS=1 RB_PW_HARNESS_DIR="$base" RB_PW_HARNESS_PULSE="$with_pulse"
    export XDG_RUNTIME_DIR="$base/run" PIPEWIRE_RUNTIME_DIR="$base/run" PULSE_RUNTIME_PATH="$base/run/pulse"
    export XDG_CONFIG_HOME="$base/config" XDG_STATE_HOME="$base/state"
    # Keeps GLib (inside WirePlumber) from starting GVFS on the private bus.
    export GIO_USE_VFS=local
    unset PIPEWIRE_REMOTE PIPEWIRE_CONFIG_DIR PULSE_SERVER DISPLAY WAYLAND_DISPLAY DBUS_SESSION_BUS_ADDRESS

    status=0
    dbus-run-session -- "$0" --inside "$@" || status=$?
    exit "$status"
fi

# From here on we run inside the private D-Bus session.
shift
logs="$RB_PW_HARNESS_DIR/logs"
pids=()

stop_daemons() {
    for ((i = ${#pids[@]} - 1; i >= 0; i--)); do
        kill "${pids[i]}" 2>/dev/null || true
    done
    for pid in "${pids[@]}"; do
        wait "$pid" 2>/dev/null || true
    done
}
trap stop_daemons EXIT
trap 'exit 143' TERM INT

print_logs() {
    for log in "$logs"/*.log; do
        [ -s "$log" ] || continue
        printf '\n---- %s (last 40 lines) ----\n' "$(basename "$log")" >&2
        tail -n 40 "$log" >&2
    done
}

# wait_for DESCRIPTION COMMAND [ARGS...]: polls the command for up to 10 s.
wait_for() {
    local what=$1
    shift
    for _ in $(seq 200); do
        if "$@" >/dev/null 2>&1; then
            return 0
        fi
        sleep 0.05
    done
    echo "run-private-pipewire: timed out waiting for $what" >&2
    print_logs
    exit 1
}

has_default_sink() {
    pw-metadata -n default | grep -q '"rbtest.sink.stereo"'
}

# Only the daemon gets PIPEWIRE_CONFIG_DIR: clients would look for client.conf there.
PIPEWIRE_CONFIG_DIR="$RB_PW_HARNESS_DIR/pipewire" pipewire >"$logs/pipewire.log" 2>&1 &
pids+=($!)
wait_for "the PipeWire socket" test -S "$XDG_RUNTIME_DIR/pipewire-0"

wireplumber --profile reverblabr-test >"$logs/wireplumber.log" 2>&1 &
pids+=($!)
if [ "$RB_PW_HARNESS_PULSE" = 1 ]; then
    pipewire-pulse >"$logs/pipewire-pulse.log" 2>&1 &
    pids+=($!)
    wait_for "the Pulse socket" test -S "$PULSE_RUNTIME_PATH/native"
fi
wait_for "WirePlumber to pick the default sink" has_default_sink

status=0
"$@" || status=$?
if [ "$status" -ne 0 ]; then
    print_logs
fi
# **A kill with nothing said is most likely the real-time time limit**.
# A PipeWire client that rtkit makes real-time is held to rtkit's
# RLIMIT_RTTIME, 200 ms with the soft limit equal to the hard one, so the
# kernel sends SIGKILL without a SIGXCPU first. A data loop whose every
# cycle overruns its period never sleeps and reaches it within a second.
# (With the soft limit lowered below the hard one, the same overrun ends in
# SIGXCPU, "CPU time limit exceeded", which names the limit: that is how it
# was confirmed.)
if [ "$status" -eq 137 ] || [ "$status" -eq 152 ]; then
    cat >&2 <<'EOF'

run-private-pipewire: the test was killed (SIGKILL or SIGXCPU). With no sanitizer
report and no OOM kill in the kernel log, the likely cause is RLIMIT_RTTIME:
a real-time data loop ran 200 ms without sleeping, which a session does when
every cycle overruns its period. A test whose load is heavy for its build
(sanitizers, a loaded machine) is the first place to look.
EOF
fi
exit "$status"

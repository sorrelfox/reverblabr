#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Checks that the tools and libraries needed to build Reverblabr are installed
# and new enough, and prints the versions it found. Exits with status 1 when a
# required check fails; missing optional tools only produce warnings.
#
# Usage: tools/check-deps.sh

set -uo pipefail

# Minimum versions: the oldest supported platform (Kubuntu 26.04) sets them.
# Keep in sync with CMakeLists.txt and docs/development.md.
readonly MIN_CMAKE=3.24
readonly MIN_GCC=15
readonly MIN_CLANG=21
readonly MIN_QT=6.10
readonly MIN_KF=6.24
readonly MIN_PIPEWIRE=1.6

failures=0
warnings=0

work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT

# True when version $1 is at least version $2.
version_ge() {
    [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n 1)" = "$2" ]
}

# Prints the first dotted version number found on stdin.
first_version() {
    grep -oE '[0-9]+(\.[0-9]+)+' | head -n 1
}

# Prints the version reported by a command, or nothing if it isn't installed.
tool_version() {
    command -v "$1" >/dev/null 2>&1 || return 0
    "$@" 2>/dev/null | first_version
}

section() {
    printf '\n%s\n' "$1"
}

row() {
    printf '  %-5s %-36s %-10s %s\n' "$1" "$2" "$3" "$4" | sed 's/[[:space:]]*$//'
}

# check LEVEL NAME FOUND [MINIMUM]
# LEVEL is "required" or "optional"; FOUND is empty when the item is missing.
check() {
    local level=$1 name=$2 found=$3 minimum=${4:-}
    local need=""
    [ -n "$minimum" ] && need=">= $minimum"
    if [ -n "$found" ] && { [ -z "$minimum" ] || version_ge "$found" "$minimum"; }; then
        row ok "$name" "$found" "$need"
    elif [ "$level" = required ]; then
        row FAIL "$name" "${found:-missing}" "$need"
        failures=$((failures + 1))
    else
        row warn "$name" "${found:-missing}" "${need:+$need, }optional"
        warnings=$((warnings + 1))
    fi
}

printf 'Reverblabr dependency check\n'
printf 'Minimums: CMake %s, GCC %s or Clang %s, Qt %s, KDE Frameworks %s, PipeWire %s\n' \
    "$MIN_CMAKE" "$MIN_GCC" "$MIN_CLANG" "$MIN_QT" "$MIN_KF" "$MIN_PIPEWIRE"

section "Build tools"
check required "CMake" "$(tool_version cmake --version)" "$MIN_CMAKE"
check required "Ninja" "$(tool_version ninja --version)"
check required "pkg-config" "$(tool_version pkg-config --version)"
check required "Git" "$(tool_version git --version)"
check required "Python 3 (tests)" "$(tool_version python3 --version)"
check required "xgettext (string extraction)" "$(tool_version xgettext --version)"
check required "msgfmt (catalog compiler)" "$(tool_version msgfmt --version)"

# One of the two compilers must be new enough. GCC is the default; Clang is
# also needed for the real-time sanitizer preset.
gcc_version=$(tool_version g++ -dumpfullversion)
clang_version=$(tool_version clang++ -dumpversion)
gcc_ok=false
clang_ok=false
[ -n "$gcc_version" ] && version_ge "$gcc_version" "$MIN_GCC" && gcc_ok=true
[ -n "$clang_version" ] && version_ge "$clang_version" "$MIN_CLANG" && clang_ok=true
if $gcc_ok || $clang_ok; then
    check optional "GCC (g++)" "$gcc_version" "$MIN_GCC"
    check optional "Clang (clang++)" "$clang_version" "$MIN_CLANG"
else
    check required "GCC (g++)" "$gcc_version" "$MIN_GCC"
    check required "Clang (clang++)" "$clang_version" "$MIN_CLANG"
fi

section "Libraries"
check required "PipeWire (libpipewire-0.3)" "$(pkg-config --modversion libpipewire-0.3 2>/dev/null)" "$MIN_PIPEWIRE"
# The Recorder's files, and the DSP test kit's fixtures. 1.1 is the
# first to write MP3.
check required "libsndfile" "$(pkg-config --modversion sndfile 2>/dev/null)" "1.1.0"
# A recording at another rate than the session's.
check required "soxr" "$(pkg-config --modversion soxr 2>/dev/null)" "0.1.3"
# Speech Denoise (ADR-0016). Optional: Kubuntu 26.04 has no package,
# and a build without it leaves that one block out.
check optional "RNNoise (Speech Denoise)" "$(pkg-config --modversion rnnoise 2>/dev/null)" "0.2"
# A recording's tags.
check required "TagLib" "$(pkg-config --modversion taglib 2>/dev/null)" "2.0"
# Pitch Shift: SoundTouch. 2.1 has the settings the block sets.
check required "SoundTouch" "$(pkg-config --modversion soundtouch 2>/dev/null)" "2.1"
# M4A recordings. Which encoders it has is the app's to find out.
check required "FFmpeg (libavformat)" "$(pkg-config --modversion libavformat 2>/dev/null)" "60"
check required "FFmpeg (libavcodec)" "$(pkg-config --modversion libavcodec 2>/dev/null)" "60"
# The Broadcast block's secure connection (TLS).
check required "GnuTLS" "$(pkg-config --modversion gnutls 2>/dev/null)" "3.7"
# LV2 plugins, found and described in the plugin host.
# Optional: the Live Stream block draws its picture with these (ADR-0027).
check optional "FreeType (the Live Stream block)" "$(pkg-config --modversion freetype2 2>/dev/null)"
check optional "HarfBuzz (the Live Stream block)" "$(pkg-config --modversion harfbuzz 2>/dev/null)"
check optional "fontconfig (the Live Stream block)" "$(pkg-config --modversion fontconfig 2>/dev/null)"
check optional "FFmpeg's libswscale (the Live Stream block)" "$(pkg-config --modversion libswscale 2>/dev/null)" "7"
check required "lilv" "$(pkg-config --modversion lilv-0 2>/dev/null)" "0.24"

# The recordings index is SQLite through Qt's driver, which is a
# plugin: the headers can be there without it.
if [ -n "$(find /usr/lib*/ /usr/lib/*/ -path '*qt6/plugins/sqldrivers/libqsqlite.so' 2>/dev/null | head -1)" ]; then
    check required "Qt6 SQLite driver" "found"
else
    check required "Qt6 SQLite driver" ""
fi

# Ask CMake itself, so the check finds packages exactly the way the build does.
mkdir -p "$work_dir/probe"
cat > "$work_dir/probe/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.24)
project(reverblabr_depcheck LANGUAGES CXX)
function(report name value)
    message(STATUS "DEPCHECK|${name}|${value}")
endfunction()
find_package(ECM QUIET NO_MODULE)
report(ECM "${ECM_VERSION}")
set(qt_components Core Gui Widgets DBus Qml Quick QuickControls2 Sql Svg Test QuickTest ShaderTools)
find_package(Qt6 QUIET OPTIONAL_COMPONENTS ${qt_components})
foreach(component IN LISTS qt_components)
    report(Qt6${component} "${Qt6${component}_VERSION}")
endforeach()
if(QT6_INSTALL_PREFIX)
    report(qml-dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_QML}")
    report(qt-bin-dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
endif()
foreach(component Kirigami I18n CoreAddons DBusAddons WindowSystem KIO GuiAddons Notifications StatusNotifierItem GlobalAccel)
    find_package(KF6${component} QUIET CONFIG)
    report(KF6${component} "${KF6${component}_VERSION}")
endforeach()
find_package(Qt6 QUIET OPTIONAL_COMPONENTS WaylandClientPrivate)
report(Qt6WaylandClientPrivate "${Qt6WaylandClientPrivate_VERSION}")
find_package(KF6Auth QUIET CONFIG)
report(KF6Auth "${KF6Auth_VERSION}")
find_package(Qt6Keychain QUIET CONFIG)
report(Qt6Keychain "${Qt6Keychain_VERSION}")
find_package(KF6DocTools QUIET CONFIG)
report(KF6DocTools "${KF6DocTools_VERSION}")
find_package(GTest QUIET CONFIG)
report(GTest "${GTest_VERSION}")
EOF

declare -A probe=()
generator=()
command -v ninja >/dev/null 2>&1 && generator=(-G Ninja)
if command -v cmake >/dev/null 2>&1 &&
    cmake -S "$work_dir/probe" -B "$work_dir/probe/build" "${generator[@]}" >"$work_dir/probe.log" 2>&1; then
    while IFS='|' read -r _ key value; do
        probe[$key]=$value
    done < <(grep -o 'DEPCHECK|.*' "$work_dir/probe.log")
else
    row FAIL "CMake package probe" "failed" ""
    failures=$((failures + 1))
    if [ -s "$work_dir/probe.log" ]; then
        tail -n 15 "$work_dir/probe.log" | sed 's/^/        /'
    fi
fi

check required "extra-cmake-modules (ECM)" "${probe[ECM]:-}" "$MIN_KF"
for component in Core Gui Widgets DBus Qml Quick QuickControls2 Sql Svg Test QuickTest; do
    check required "Qt6 $component" "${probe[Qt6$component]:-}" "$MIN_QT"
done
check optional "Qt6 ShaderTools" "${probe[Qt6ShaderTools]:-}" "$MIN_QT"
# Optional: without Qt's Wayland client internals, a right click on the
# frameless window's header opens a menu of our own rather than the
# compositor's window menu.
check optional "Qt6 Wayland client internals (the compositor's window menu)" \
    "${probe[Qt6WaylandClientPrivate]:-}" "$MIN_QT"
for component in Kirigami I18n CoreAddons DBusAddons WindowSystem KIO GuiAddons Notifications StatusNotifierItem GlobalAccel; do
    check required "KF6 $component" "${probe[KF6$component]:-}" "$MIN_KF"
done
# Optional: the Broadcast block, whose password is kept in the desktop's
# secret store (ADR-0026). Without it that one block is left out.
check optional "QtKeychain (the Broadcast block)" "${probe[Qt6Keychain]:-}" "0.14"
# Optional: without KAuth the app builds without the all-users virtual
# devices helper, and a device for all users is refused as needing it.
check optional "KF6 Auth (virtual devices for all users)" "${probe[KF6Auth]:-}" "$MIN_KF"
# Optional: without KDocTools there is no handbook for F1 to open.
check optional "KF6 DocTools (the handbook)" "${probe[KF6DocTools]:-}" "$MIN_KF"
if command -v pkaction >/dev/null 2>&1; then
    check optional "polkit (the administrator's password prompt)" "found"
else
    check optional "polkit (the administrator's password prompt)" ""
fi
check required "GoogleTest" "${probe[GTest]:-}"

section "QML modules (needed to run the app and the QML tests)"
qml_dir=${probe[qml-dir]:-}
qml_module() {
    local name=$1 path=$2
    if [ -n "$qml_dir" ] && [ -f "$qml_dir/$path/qmldir" ]; then
        row ok "$name" "found" ""
    else
        row FAIL "$name" "missing" ""
        failures=$((failures + 1))
    fi
}
qml_module "QtQuick.Controls" "QtQuick/Controls"
qml_module "QtTest" "QtTest"
qml_module "org.kde.kirigami" "org/kde/kirigami"
qml_module "org.kde.ki18n" "org/kde/ki18n"
qml_module "org.kde.desktop (Breeze controls)" "org/kde/desktop"

section "PipeWire integration tests (tests/pw/harness)"
for tool in pipewire wireplumber pipewire-pulse pw-cat pw-record pw-dump pw-metadata pw-link pw-loopback wpctl paplay pactl dbus-run-session; do
    if command -v "$tool" >/dev/null 2>&1; then
        check required "$tool" "found"
    else
        check required "$tool" ""
    fi
done
# Only the notification-sound test needs it, and it skips without.
# No grep -q: with pipefail, grep exiting early would fail the pipeline.
if /sbin/ldconfig -p 2>/dev/null | grep 'libcanberra\.so\.0' >/dev/null; then
    check optional "libcanberra (notification test)" "found"
else
    check optional "libcanberra (notification test)" ""
fi
if [ -f /usr/share/pipewire/pipewire.conf ]; then
    row ok "/usr/share/pipewire/pipewire.conf" "found" ""
else
    row FAIL "/usr/share/pipewire/pipewire.conf" "missing" ""
    failures=$((failures + 1))
fi

section "Development tools (optional)"
rtsan=""
if $clang_ok; then
    printf 'int main() { return 0; }\n' > "$work_dir/rtsan.cpp"
    if clang++ -fsanitize=realtime "$work_dir/rtsan.cpp" -o "$work_dir/rtsan" >/dev/null 2>&1 &&
        "$work_dir/rtsan" >/dev/null 2>&1; then
        rtsan=works
    fi
fi
check optional "Real-time sanitizer (Clang)" "$rtsan"
qmllint=""
for candidate in "${probe[qt-bin-dir]:-}/qmllint" qmllint6 qmllint-qt6 qmllint; do
    if command -v "$candidate" >/dev/null 2>&1; then
        qmllint=$(tool_version "$candidate" --version)
        break
    fi
done
check optional "qmllint" "$qmllint"
check optional "clang-format" "$(tool_version clang-format --version)"
check optional "clang-tidy" "$(tool_version clang-tidy --version)"
check optional "reuse (licence lint)" "$(tool_version reuse --version)"
for tool in pw-top pw-cli wpctl qpwgraph; do
    if command -v "$tool" >/dev/null 2>&1; then
        check optional "$tool" "found"
    else
        check optional "$tool" ""
    fi
done

section "Environment (for information only)"
row info "Session type" "${XDG_SESSION_TYPE:-none}" "Reverblabr supports Plasma on Wayland"
row info "Desktop" "${XDG_CURRENT_DESKTOP:-none}" ""
pipewire_server=""
if command -v pw-cli >/dev/null 2>&1; then
    pipewire_server=$(timeout 3 pw-cli info 0 2>/dev/null | sed -n 's/.*version: "\([^"]*\)".*/\1/p' | head -n 1)
fi
row info "PipeWire server" "${pipewire_server:-not running}" ""
row info "WirePlumber" "$(tool_version wireplumber --version || true)" ""

printf '\n'
if [ "$failures" -gt 0 ]; then
    printf 'Result: %d required check(s) failed, %d warning(s). Package lists are in tools/deps/.\n' \
        "$failures" "$warnings"
    exit 1
fi
printf 'Result: all required checks passed, %d warning(s).\n' "$warnings"

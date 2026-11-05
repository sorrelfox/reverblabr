#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Installs the built package on a clean Debian or Ubuntu and checks it, the
# way a user gets it: apt pulls in what it needs, every program starts and
# says the package's version, the files are where the desktop, the session
# bus, systemd and polkit look for them, and the package purges cleanly.
#
#   packaging/debian/install-check.sh DEBDIR
#
# DEBDIR holds the .deb (any folder layout). Run as root in a throwaway
# container of the distribution the package was built for: it installs and
# removes system packages.

set -euo pipefail

die() {
    printf 'install-check: %s\n' "$*" >&2
    exit 1
}

[ $# -eq 1 ] || die "usage: packaging/debian/install-check.sh DEBDIR"
[ "$(id -u)" -eq 0 ] || die "run as root, in a throwaway container"

mapfile -t debs < <(find "$(realpath -- "$1")" -name 'reverblabr_*.deb')
[ ${#debs[@]} -eq 1 ] || die "expected one reverblabr_*.deb in $1, found ${#debs[@]}"

export DEBIAN_FRONTEND=noninteractive
{ apt-get update && apt-get install -y "${debs[0]}"; } >/tmp/apt.log 2>&1 \
    || { tail -40 /tmp/apt.log; die "apt could not install ${debs[0]}"; }
package_version=$(dpkg-query -W -f '${Version}' reverblabr)
version=${package_version%%-*}
echo "Installed reverblabr $package_version"

case "$(dpkg --print-architecture)" in
    amd64) multiarch=x86_64-linux-gnu ;;
    arm64) multiarch=aarch64-linux-gnu ;;
    *) die "no multiarch folder known for $(dpkg --print-architecture)" ;;
esac
libexec=/usr/lib/$multiarch/libexec

# The model host is there when the build had ONNX Runtime.
programs=(reverblabr reverblabrd reverblabr-ctl "$libexec/reverblabr-plugin-host")
[ -e "$libexec/reverblabr-model-host" ] && programs+=("$libexec/reverblabr-model-host")

# The window needs a display for nothing but its window; --version answers
# before it opens one.
export QT_QPA_PLATFORM=offscreen
for program in "${programs[@]}"; do
    answer=$("$program" --version 2>/dev/null | head -n 1)
    echo "$program: $answer"
    [ "${answer##* }" = "$version" ] || die "$program says \"$answer\", the package is $version"
done

# packaging/README.md lists them; without the bus and systemd files every
# window would run an engine of its own.
expected=(
    /usr/bin/reverblabr
    /usr/bin/reverblabrd
    /usr/bin/reverblabr-ctl
    "$libexec/reverblabr-plugin-host"
    /usr/share/applications/io.github.reverblabr.Reverblabr.desktop
    /usr/share/metainfo/io.github.reverblabr.Reverblabr.metainfo.xml
    /usr/share/icons/hicolor/scalable/apps/io.github.reverblabr.Reverblabr.svg
    /usr/share/icons/hicolor/16x16/apps/io.github.reverblabr.Reverblabr.svg
    /usr/share/knotifications6/reverblabr.notifyrc
    /usr/share/dbus-1/services/io.github.reverblabr.Engine.service
    /usr/share/krunner/dbusplugins/io.github.reverblabr.Runner.desktop
    /usr/lib/systemd/user/reverblabrd.service
    /usr/share/man/man1/reverblabr-ctl.1.gz
    /usr/share/doc/HTML/en/reverblabr/index.cache.bz2
    /usr/share/doc/HTML/en/reverblabr/index.docbook
    /usr/share/bash-completion/completions/reverblabr-ctl
    /usr/share/zsh/vendor-completions/_reverblabr-ctl
    /usr/lib/kf6/kauth/libexec/reverblabr-virtualdevices-helper
    /usr/share/dbus-1/system.d/io.github.reverblabr.virtualdevices.conf
    /usr/share/dbus-1/system-services/io.github.reverblabr.virtualdevices.service
    /usr/share/polkit-1/actions/io.github.reverblabr.virtualdevices.policy
    /usr/share/plasma/plasmoids/io.github.reverblabr.sessions/metadata.json
    /usr/share/plasma/plasmoids/io.github.reverblabr.sessions/contents/ui/main.qml
    "/usr/lib/$multiarch/qt6/qml/io/github/reverblabr/plasmoid/qmldir"
    "/usr/lib/$multiarch/qt6/qml/io/github/reverblabr/plasmoid/libreverblabr-plasmoidplugin.so"
)
listed=$(dpkg -L reverblabr)
for file in "${expected[@]}"; do
    grep -qxF "$file" <<<"$listed" || die "the package has no $file"
    # Ubuntu's container images leave out documentation (dpkg's path
    # excludes), so the manual page and the handbook are in the package but
    # not on the disk.
    [ -e "$file" ] || [[ $file == /usr/share/man/* || $file == /usr/share/doc/* ]] \
        || die "$file is listed but not installed"
done
grep -q '^Exec=/usr/bin/reverblabrd$' /usr/share/dbus-1/services/io.github.reverblabr.Engine.service \
    || die "the D-Bus activation file does not start /usr/bin/reverblabrd"
grep -q '^X-Plasma-DBusRunner-Service=io.github.reverblabr.Engine$' \
    /usr/share/krunner/dbusplugins/io.github.reverblabr.Runner.desktop \
    || die "KRunner's runner file does not name the engine"
# Auto Run enables the unit, not the package.
if compgen -G '/etc/systemd/user/*.wants/reverblabrd.service' >/dev/null; then
    die "installing the package enabled reverblabrd.service"
fi
if command -v systemctl >/dev/null && systemctl --global is-enabled reverblabrd.service >/dev/null 2>&1; then
    die "installing the package enabled reverblabrd.service"
fi
echo "All ${#expected[@]} files are in place"

apt-get purge -y reverblabr >/tmp/apt.log 2>&1 || { tail -40 /tmp/apt.log; die "apt could not remove reverblabr"; }
for file in "${expected[@]}"; do
    [ ! -e "$file" ] || die "$file is still there after the package was removed"
done
echo "PASS: install check"

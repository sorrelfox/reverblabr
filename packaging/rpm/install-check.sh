#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Installs the built packages on a clean Fedora and checks them, the way a
# user gets them: dnf pulls in what they need, every program starts and
# says the package's version, the files are where the desktop, the session
# bus, systemd and polkit look for them, and the package removes cleanly.
#
#   packaging/rpm/install-check.sh RPMDIR
#
# RPMDIR holds the binary RPMs (any folder layout). Run as root in a
# throwaway container: it installs and removes system packages.

set -euo pipefail

die() {
    printf 'install-check: %s\n' "$*" >&2
    exit 1
}

[ $# -eq 1 ] || die "usage: packaging/rpm/install-check.sh RPMDIR"
[ "$(id -u)" -eq 0 ] || die "run as root, in a throwaway container"

mapfile -t rpms < <(find "$1" -name 'reverblabr-*.rpm' ! -name '*.src.rpm' \
    ! -name '*-debuginfo-*' ! -name '*-debugsource-*')
[ ${#rpms[@]} -gt 0 ] || die "no binary RPMs in $1"

dnf -y install "${rpms[@]}" >/tmp/dnf.log 2>&1 || { tail -40 /tmp/dnf.log; die "dnf could not install ${rpms[*]}"; }
version=$(rpm -q --qf '%{VERSION}' reverblabr)
echo "Installed reverblabr $version"

# The window needs a display for nothing but its window; --version answers
# before it opens one.
export QT_QPA_PLATFORM=offscreen
for program in reverblabr reverblabrd reverblabr-ctl /usr/libexec/reverblabr-plugin-host; do
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
    /usr/libexec/reverblabr-plugin-host
    /usr/share/applications/io.github.reverblabr.Reverblabr.desktop
    /usr/share/metainfo/io.github.reverblabr.Reverblabr.metainfo.xml
    /usr/share/icons/hicolor/scalable/apps/io.github.reverblabr.Reverblabr.svg
    /usr/share/icons/hicolor/16x16/apps/io.github.reverblabr.Reverblabr.svg
    /usr/share/knotifications6/reverblabr.notifyrc
    /usr/share/dbus-1/services/io.github.reverblabr.Engine.service
    /usr/share/krunner/dbusplugins/io.github.reverblabr.Runner.desktop
    /usr/lib/systemd/user/reverblabrd.service
    /usr/share/man/man1/reverblabr-ctl.1.gz
    /usr/share/bash-completion/completions/reverblabr-ctl
    /usr/share/zsh/site-functions/_reverblabr-ctl
    /usr/libexec/kf6/kauth/reverblabr-virtualdevices-helper
    /usr/share/dbus-1/system.d/io.github.reverblabr.virtualdevices.conf
    /usr/share/dbus-1/system-services/io.github.reverblabr.virtualdevices.service
    /usr/share/polkit-1/actions/io.github.reverblabr.virtualdevices.policy
    /usr/share/plasma/plasmoids/io.github.reverblabr.sessions/metadata.json
    /usr/share/plasma/plasmoids/io.github.reverblabr.sessions/contents/ui/main.qml
    /usr/lib64/qt6/qml/io/github/reverblabr/plasmoid/qmldir
    /usr/lib64/qt6/qml/io/github/reverblabr/plasmoid/libreverblabr-plasmoidplugin.so
)
listed=$(rpm -ql reverblabr)
for file in "${expected[@]}"; do
    grep -qxF "$file" <<<"$listed" || die "the package has no $file"
    # Fedora's container images install no documentation (tsflags=nodocs),
    # so the manual page is in the package but not on the disk.
    [ -e "$file" ] || [[ $file == /usr/share/man/* ]] || die "$file is listed but not installed"
done
grep -q '^Exec=/usr/bin/reverblabrd$' /usr/share/dbus-1/services/io.github.reverblabr.Engine.service \
    || die "the D-Bus activation file does not start /usr/bin/reverblabrd"
grep -q '^X-Plasma-DBusRunner-Service=io.github.reverblabr.Engine$' \
    /usr/share/krunner/dbusplugins/io.github.reverblabr.Runner.desktop \
    || die "KRunner's runner file does not name the engine"
# Auto Run enables the unit, not the package.
if systemctl --global is-enabled reverblabrd.service >/dev/null 2>&1; then
    die "installing the package enabled reverblabrd.service"
fi
echo "All ${#expected[@]} files are in place"

dnf -y remove reverblabr >/tmp/dnf.log 2>&1 || { tail -40 /tmp/dnf.log; die "dnf could not remove reverblabr"; }
for file in "${expected[@]}"; do
    [ ! -e "$file" ] || die "$file is still there after the package was removed"
done
echo "PASS: install check"

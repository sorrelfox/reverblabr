#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
#
# Builds the AppImage from a release tarball, in a plain Fedora 44 system
# (ADR-0037):
#
#   packaging/appimage/build-appimage.sh dist/reverblabr-X.Y.Z.tar.xz [OUTDIR]
#
# Reverblabr is built from the tarball as the Fedora package is, every block
# included, and installed into an AppDir with everything its programs load:
# Qt, KDE Frameworks, the Qt plugins and QML modules the window uses, and the
# C library with its dynamic loader. **Only the system's graphics libraries
# and PipeWire's client library are left to the system** (the list below):
# they belong with its drivers and with the PipeWire it runs. Each program
# starts through packaging/appimage/launch, which hands it to the AppImage's
# loader, so the AppImage runs where glibc is older than Fedora's.
#
# As root, the script installs what it needs with dnf: the Fedora package's
# BuildRequires, from the tarball's spec, and the parts that are loaded at
# run time without being linked. The AppImage's runtime, AppImage's type2
# runtime, is downloaded from its release on GitHub and checked against the
# checksum below, unless RB_APPIMAGE_RUNTIME names a copy of it.
# RB_APPIMAGE_JOBS caps the compilers run at once (default: one a CPU).
#
# OUTDIR (default: the tarball's folder) gets reverblabr-X.Y.Z-x86_64.AppImage
# and build-appimage.log; the work is done in OUTDIR/appimage-work, which is
# left for a look and removed by the next run.

set -euo pipefail

runtime_url=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
runtime_sha256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d

die() {
    printf 'build-appimage: %s\n' "$*" >&2
    exit 1
}

[ $# -ge 1 ] && [ $# -le 2 ] || die "usage: packaging/appimage/build-appimage.sh reverblabr-X.Y.Z.tar.xz [OUTDIR]"
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
[ "$(uname -m)" = x86_64 ] || die "the AppImage is built for x86_64, and this is $(uname -m)"
. /etc/os-release
[ "$ID" = fedora ] || die "this is $ID; the AppImage is built on Fedora, whose package it follows"

work=$outdir/appimage-work
log=$outdir/build-appimage.log
rm -rf -- "$work"
mkdir -p -- "$work"
: > "$log"
step() {
    printf '== %s\n' "$*" | tee -a "$log"
}
quiet() {
    "$@" >> "$log" 2>&1 || {
        tail -n 60 "$log" >&2
        die "failed: $*"
    }
}

tar -xJf "$tarball" -C "$work"
src=$work/$top
[ -d "$src" ] || die "the tarball has no $top/"

# Loaded at run time, not linked, so the spec doesn't name them: the window
# on Wayland, the desktop's look (KDE's platform theme, the Breeze widget
# style and Plasma's QML controls), the floating label over other windows,
# Qt's own translations and KIO's (which Fedora puts in kf6-kio-doc), and the
# tools below.
runtime_packages=(
    qt6-qtwayland plasma-integration plasma-breeze kf6-qqc2-desktop-style layer-shell-qt
    qt6-qttranslations kf6-kio-doc
    patchelf squashfs-tools librsvg2-tools desktop-file-utils file
)
as_root=false
[ "$(id -u)" -eq 0 ] && as_root=true
if $as_root; then
    step "dependencies"
    # **Every language**: a container image installs only English, and the
    # AppImage carries Qt's and KDE's own translations.
    rm -f /etc/rpm/macros.image-language-conf
    # The spec's BuildRequires; its version and changelog are not needed.
    sed -e 's/@VERSION@/0.0.0/g' -e '/^%changelog/,$d' "$src/packaging/rpm/reverblabr.spec.in" > "$work/reverblabr.spec"
    quiet dnf -y install 'dnf-command(builddep)' rpm-build
    quiet dnf -y builddep "$work/reverblabr.spec"
    quiet dnf -y install "${runtime_packages[@]}"
fi

step "building Reverblabr $version"
# As the Fedora package, but without the panel widget and the all-users
# helper, which an AppImage can't install (ADR-0037), and without the tests,
# which the release runs from the tarball on its own.
quiet cmake -S "$src" -B "$work/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DBUILD_TESTING=OFF \
    -DRB_BUILD_HANDBOOK=ON \
    -DRB_BUILD_SPEECH_DENOISE=ON \
    -DRB_BUILD_CLAP=ON \
    -DRB_BUILD_VST3=ON \
    -DRB_BUILD_BROADCAST=ON \
    -DRB_BUILD_LIVESTREAM=ON \
    -DRB_BUILD_MODEL_BLOCK=ON \
    -DRB_BUILD_PLASMOID=OFF \
    -DRB_BUILD_AUTH_HELPER=OFF
quiet cmake --build "$work/build" -j "${RB_APPIMAGE_JOBS:-$(nproc)}"
stage=$work/stage
DESTDIR=$stage quiet cmake --install "$work/build"

step "the AppDir"
appdir=$work/AppDir
usr=$appdir/usr
lib=$usr/lib
own=$lib/reverblabr-appimage
mkdir -p "$usr/bin" "$own/programs" "$own/names" "$usr/libexec/kf6" "$usr/share/reverblabr"

# Reverblabr's programs, and KIO's, which runs a file operation's worker.
found() {
    local file
    file=$(find "$1" -name "$2" -type f -perm -u+x | head -n 1)
    [ -n "$file" ] || die "the install has no $2"
    printf '%s\n' "$file"
}
programs=(reverblabr reverblabrd reverblabr-ctl reverblabr-plugin-host reverblabr-model-host kioworker)
for program in reverblabr reverblabrd reverblabr-ctl reverblabr-plugin-host reverblabr-model-host; do
    install -m 0755 "$(found "$stage" "$program")" "$own/programs/$program"
done
install -m 0755 /usr/libexec/kf6/kioworker "$own/programs/kioworker"
# An install's RUNPATH would put a system folder before the AppImage's.
for program in "$own"/programs/reverblabr*; do
    patchelf --remove-rpath "$program"
    strip --strip-unneeded "$program"
done

# The loader, in usr/bin, and a link to it named after each program, which is
# what the system shows the process as.
loader=$(realpath /lib64/ld-linux-x86-64.so.2)
install -m 0755 "$loader" "$usr/bin/ld-linux-x86-64.so.2"
install -m 0755 "$src/packaging/appimage/launch" "$own/launch"
for program in "${programs[@]}"; do
    ln -s ../../../bin/ld-linux-x86-64.so.2 "$own/names/$program"
done
for program in reverblabr reverblabrd reverblabr-ctl reverblabr-plugin-host reverblabr-model-host; do
    ln -s ../lib/reverblabr-appimage/launch "$usr/bin/$program"
done
# KIO looks for kioworker beside its own library, in ../libexec/kf6.
ln -s ../../lib/reverblabr-appimage/launch "$usr/libexec/kf6/kioworker"
# Qt's folders, from the program's own: usr/bin, where the loader is.
cat > "$usr/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Libraries = lib
LibraryExecutables = libexec
Plugins = lib/qt6/plugins
QmlImports = lib/qt6/qml
Data = share
Translations = share/qt6/translations
EOF
# **What tells the programs they run from an AppImage** (rb::pw::inAppImage):
# the window starts the engine itself, and the engine keeps no Auto Run unit
# and no timers.
printf 'Reverblabr runs from an AppImage here (ADR-0037).\n' > "$usr/share/reverblabr/appimage"

# The Qt plugins the window loads by name: the Wayland and X11 platforms and
# the offscreen one, Wayland's shells and decorations, the desktop's look,
# input methods, icons and images, the recordings index, TLS for KIO, the
# frameless window's shadow, Kirigami's styles and Reverblabr's own, and the
# workers for the Files tab.
qtplugins=/usr/lib64/qt6/plugins
plugin_files=(
    platforms/libqwayland.so platforms/libqxcb.so platforms/libqoffscreen.so platforms/libqminimal.so
    'wayland-decoration-client/*.so' 'wayland-graphics-integration-client/*.so' 'wayland-shell-integration/*.so'
    'xcbglintegrations/*.so'
    platformthemes/KDEPlasmaPlatformTheme6.so platformthemes/libqxdgdesktopportal.so
    'platforminputcontexts/*.so'
    'iconengines/*.so' 'kiconthemes6/iconengines/*.so'
    imageformats/libqsvg.so imageformats/libqjpeg.so imageformats/libqgif.so imageformats/libqico.so
    sqldrivers/libqsqlite.so
    styles/breeze6.so
    'tls/*.so'
    'networkinformation/*.so'
    kf6/FrameworkIntegrationPlugin.so kf6/ktranscript.so
    'kf6/kwindowsystem/*.so'
    'kf6/kirigami/platform/*.so'
    kf6/kio/kio_file.so kf6/kio/kio_trash.so
)
for pattern in "${plugin_files[@]}"; do
    matched=no
    for file in $qtplugins/$pattern; do
        [ -e "$file" ] || continue
        matched=yes
        mkdir -p "$lib/qt6/plugins/$(dirname -- "${file#"$qtplugins"/}")"
        cp -L "$file" "$lib/qt6/plugins/${file#"$qtplugins"/}"
    done
    [ "$matched" = yes ] || die "Qt has no plugin $pattern"
done
cp -L "$(found "$stage" Reverblabr.so)" "$lib/qt6/plugins/kf6/kirigami/platform/"

# The QML modules the window imports, and those of the desktop's controls,
# which are chosen at run time: each module's own files, and its folders
# that are not modules of their own. Not the Qt Quick Controls styles the
# window never uses.
qml=/usr/lib64/qt6/qml
roots=()
while IFS= read -r folder; do
    roots+=(-rootPath "$folder")
done < <(find "$src/app" "$src/blocks" -name '*.qml' -not -path '*/tests/*' -printf '%h\n' | sort -u)
roots+=(-rootPath "$qml/org/kde/desktop" -rootPath "$qml/org/kde/qqc2desktopstyle")
/usr/lib64/qt6/libexec/qmlimportscanner -importPath "$qml" "${roots[@]}" 2>> "$log" \
    | grep -o "\"path\": \"$qml/[^\"]*\"" | sed "s|^\"path\": \"$qml/||; s|\"\$||" | sort -u \
    | grep -Ev '^QtQuick/Controls/(FluentWinUI3|Imagine|Material|Universal)(/|$)' > "$work/qml-modules"
grep -qx 'org/kde/kirigami' "$work/qml-modules" || die "qmlimportscanner found no Kirigami: $(head -c 300 "$work/qml-modules")"
while IFS= read -r module; do
    mkdir -p "$lib/qt6/qml/$module"
    find "$qml/$module" -mindepth 1 -maxdepth 1 -type f -exec cp -L -t "$lib/qt6/qml/$module" {} +
    for folder in "$qml/$module"/*/; do
        [ -d "$folder" ] || continue
        find "$folder" -name qmldir -print -quit | grep -q . || cp -rL "$folder" "$lib/qt6/qml/$module/"
    done
done < "$work/qml-modules"

# A plugin's own window: suil's module for an X11 window in an X11 one.
mkdir -p "$lib/suil-0"
cp -L /usr/lib64/suil-0/libsuil_x11.so "$lib/suil-0/"

# **The libraries**: everything the programs and the plugins above link,
# found as this system's loader finds them, but not the ones left to the
# system. OpenSSL is loaded by name, by Qt's TLS plugin.
system_libraries='^(libGL|libGLX|libEGL|libGLESv1_CM|libGLESv2|libGLdispatch|libOpenGL|libgbm|libdrm|libvulkan|libpipewire-0\.3)\.so'
mapfile -t elves < <(find "$own/programs" "$lib/qt6" "$lib/suil-0" -type f \( -name '*.so*' -o -perm -u+x \) -print)
elves+=(/usr/lib64/libssl.so.3 /usr/lib64/libcrypto.so.3)
declare -A copied=()
for elf in "${elves[@]}"; do
    file -L "$elf" | grep -q ELF || continue
    while read -r soname arrow path _; do
        [ "$arrow" = "=>" ] || continue
        [ "$path" != "not" ] || die "$elf: $soname is not found"
        [[ $soname =~ $system_libraries ]] && continue
        [ -n "${copied[$soname]:-}" ] && continue
        copied[$soname]=$path
        cp -L "$path" "$lib/$soname"
    done < <(ldd "$elf")
done
for soname in libssl.so.3 libcrypto.so.3; do
    [ -n "${copied[$soname]:-}" ] || {
        copied[$soname]=/usr/lib64/$soname
        cp -L "/usr/lib64/$soname" "$lib/$soname"
    }
done
for needed in libc.so.6 libstdc++.so.6 libQt6Core.so.6 libKF6KIOCore.so.6; do
    [ -e "$lib/$needed" ] || die "$needed was not bundled"
done

step "the shared files"
# Reverblabr's own, without what only an installed copy uses: D-Bus
# activation, the user unit, KRunner's file, the manual page and the shell
# completions (ADR-0037).
cp -a "$stage/usr/share/." "$usr/share/"
rm -rf "$usr/share/dbus-1" "$usr/share/krunner" "$usr/share/man" "$usr/share/bash-completion" "$usr/share/zsh"
# The translations of every package a library came from, gettext's and
# the *_qt.qm many of KDE's Frameworks use, Qt's own too. As
# root, those packages are installed again with every language, which an
# image installed with English only (above) lacks.
mapfile -t packages < <(for path in "${copied[@]}"; do rpm -qf --qf '%{NAME}\n' "$path" 2> /dev/null || true; done | sort -u)
packages+=(qt6-qttranslations kf6-kio-doc plasma-integration plasma-breeze kf6-qqc2-desktop-style kf6-kirigami)
if $as_root; then
    quiet dnf -y reinstall --skip-unavailable "${packages[@]}"
fi
rpm -ql "${packages[@]}" 2> /dev/null | grep -E '^/usr/share/(locale/[^/]+/LC_MESSAGES/[^/]+\.(mo|qm)|qt6/translations/[^/]+\.qm)$' | sort -u \
    | while IFS= read -r file; do
        [ -e "$file" ] || continue
        mkdir -p "$appdir$(dirname -- "$file")"
        cp -L "$file" "$appdir$file"
    done
# The AppDir's own entry: the desktop entry, its icon and AppRun.
id=io.github.reverblabr.Reverblabr
install -m 0755 "$src/packaging/appimage/AppRun" "$appdir/AppRun"
cp "$usr/share/applications/$id.desktop" "$appdir/$id.desktop"
cp "$usr/share/icons/hicolor/scalable/apps/$id.svg" "$appdir/$id.svg"
rsvg-convert -w 256 -h 256 "$appdir/$id.svg" -o "$appdir/.DirIcon"
quiet desktop-file-validate "$appdir/$id.desktop"

step "the AppImage"
if [ -n "${RB_APPIMAGE_RUNTIME:-}" ]; then
    cp -- "$RB_APPIMAGE_RUNTIME" "$work/runtime"
else
    curl -fsSL --retry 3 -o "$work/runtime" "$runtime_url"
fi
echo "$runtime_sha256  $work/runtime" | sha256sum -c --quiet - || die "the runtime is not the one this script names"
quiet mksquashfs "$appdir" "$work/AppDir.squashfs" -root-owned -noappend -comp zstd -Xcompression-level 19 -b 1M
appimage=$outdir/reverblabr-$version-x86_64.AppImage
cat "$work/runtime" "$work/AppDir.squashfs" > "$appimage"
chmod 0755 "$appimage"
printf '%s libraries, %s of AppDir\n' "${#copied[@]}" "$(du -sh "$appdir" | cut -f1)" | tee -a "$log"
ls -l "$appimage"

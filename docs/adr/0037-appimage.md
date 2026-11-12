<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0037: The AppImage: built on Fedora 44 with every library and the C library, started through its own loader; the window starts the engine; no Auto Run, timers, KRunner, panel widget or all-users helper

- **Status:** Accepted
- **Date:** 2026-11-09

## Context

A release should also carry an AppImage: one file that runs without being installed, on distributions the packages don't cover. Several things decide what it can be:

- **Reverblabr needs Qt 6.10, KDE Frameworks 6.24 and PipeWire 1.6.** Every distribution that has them, Fedora 44, Ubuntu 26.04 and Debian testing, has glibc 2.43. An AppImage built the usual way, with the system's C library, would run only where glibc is at least that new, which is where the packages already are.
- **An AppImage is mounted where its runtime chooses**, `/tmp/.mount_*`, for as long as a process started from it holds the runtime's pipe open, and is gone after that. Nothing outside it can name a program in it for later: not a D-Bus activation file, not the systemd unit Auto Run enables (ADR-0021), not a timer's service, not KRunner's file (ADR-0023). Its file can be moved or deleted at any time.
- **An AppImage installs nothing.** The panel widget (ADR-0024) and the all-users helper (ADR-0013), which a desktop or root has to find in system folders, can't come with it.
- **An AppImage is not a sandbox.** It reads and writes the person's own folders as a package does, sees the plugins installed on the system, and reaches PipeWire as any program does, so the Flatpak's limits on those (ADR-0034) don't apply.
- **What a program starts inherits its environment**: the media player Open With starts, the help viewer, the player a notification opens. Variables an AppImage sets for itself reach them too.

## Decision

**Built on Fedora 44, as the Fedora package is**, from the release tarball, every block included (the Model block with Fedora's ONNX Runtime), but without the panel widget and the all-users helper (`packaging/appimage/build-appimage.sh`). The release workflow builds it and starts it on Ubuntu 24.04 (`smoke-appimage.sh`).

**Everything the programs load comes with them, the C library and its dynamic loader too.** Each program is started by `packaging/appimage/launch`, which hands it to the AppImage's own `ld-linux-x86-64.so.2` with the AppImage's library folder first (`--library-path`, which is not inherited, unlike `LD_LIBRARY_PATH`). The AppImage then runs where glibc is older than Fedora's: it has been started on Ubuntu 24.04 (glibc 2.39), Debian 13 (2.41) and AlmaLinux 9 (2.34).

**Two kinds of library are the system's:** the graphics libraries (libGL, libEGL, libgbm, libdrm, Vulkan and the drivers behind them), which belong with the system's drivers, and **PipeWire's client library**, which belongs with the PipeWire the system runs. Taking PipeWire's from the AppImage would need `PIPEWIRE_MODULE_DIR` and `SPA_PLUGIN_DIR` pointed into it, and every program Reverblabr opens would load modules of another PipeWire version. The system has to run PipeWire 1.6 anyway. The AppImage's loader finds these through the system's `ld.so.cache`, and a library built for an older glibc runs with a newer one.

**The loader names the programs' folder.** It sits in `usr/bin` beside the programs' names, and each program is started through a link to it named after the program, so the system shows the process as `reverblabrd`, not as the loader, and the program's own folder (`/proc/self/exe`) is `usr/bin`: where Qt finds `qt.conf`, where the window finds `reverblabrd`, and where KIO, which looks beside its own library, finds `kioworker` (`usr/libexec/kf6`). The programs themselves are in `usr/lib/reverblabr-appimage/programs`. `launch` points the engine at the AppImage's plugin and model hosts (`REVERBLABR_PLUGIN_HOST`, `REVERBLABR_MODEL_HOST`), puts the AppImage's shared folder first in `XDG_DATA_DIRS`, and takes `QT_PLUGIN_PATH` and the QML import paths away, so that nothing built for the system's Qt is loaded into the AppImage's.

**A file says it is an AppImage**: `usr/share/reverblabr/appimage`, found from the programs' folder (`rb::pw::inAppImage()`), as `/.flatpak-info` says a Flatpak (ADR-0034). Not `APPIMAGE` or `APPDIR`, which everything an AppImage starts inherits.

**From an AppImage, as in a Flatpak:**
- **The window starts `reverblabrd` itself**, from its own folder, detached, and never asks the bus to start one: an activation file there belongs to an installed package. The engine holds the runtime's pipe it inherited, so the AppImage stays mounted while it runs, after the window has closed, and is let go once it exits.
- **No Auto Run and no timers.** The daemon keeps no unit and no timers in step and starts no session on its own. The sessions keep their settings, and the Info tab, the Schedule tab, Settings and the Virtual Devices window say that Reverblabr from an AppImage can't do it (`AppShell.appImage`), in their own words, not the Flatpak's.
- **No KRunner, no panel widget, no all-users helper.** The activation file, the systemd unit and KRunner's file are left out, with the manual page and the shell completions.

**Unlike a Flatpak:** virtual devices are written to the person's own `~/.config/pipewire`, as in a package; the plugins installed on the system are found, and run in the AppImage's plugin host with its libraries; and the engine reconnects by itself after PipeWire restarts.

**Called by another name**, the AppImage runs another of its programs: through a link named `reverblabr-ctl`, the command-line tool, and through one named `reverblabrd`, the engine (`AppRun`, by the runtime's `ARGV0`).

**The secure connections' authorities.** Fedora's GnuTLS reads the system's store through p11-kit, which looks for its trust module in Fedora's library folder, and elsewhere finds none. When the system's store gives no authority, the Broadcast and Live Stream blocks read the distributions' bundle files instead, Fedora's first (`kAuthorityBundles`); a package built anywhere gains that too.

**The runtime is AppImage's type2 runtime**, from a dated release on GitHub, checked against its SHA-256 sum in the build script. The AppDir is a SquashFS with zstd compression. The AppImage carries no update information, as no package of Reverblabr updates itself.

## Consequences

- One file runs on most x86_64 distributions with Plasma 6 and PipeWire 1.6, without installing anything, and keeps the background engine: sessions run with the window closed, and `reverblabr-ctl` reaches them.
- It is large, about 150 MB: Qt, KDE Frameworks, FFmpeg with every library Fedora's links, ONNX Runtime, ICU and the C library.
- **The AppImage and a package share the engine's name**, as the Flatpak and a package do (ADR-0034): whichever engine takes it first serves every window.
- A plugin installed on the system runs with the AppImage's libraries, and one built for a newer glibc than Fedora 44's can't load.
- What Reverblabr starts inherits `XDG_DATA_DIRS` with the AppImage's shared folder first, and the hosts' variables, which only Reverblabr reads.
- On a system without FUSE, the AppImage has to be unpacked (`--appimage-extract`) and run from there. `--appimage-extract-and-run` deletes its copy when the window closes, under an engine that is still running.
- A library Fedora 44 updates needs a new AppImage; nothing in it updates itself.

## Alternatives considered

- **Built on an older system, with the system's C library**, as most AppImages are. None has Qt 6.10, KDE Frameworks 6.24 and PipeWire 1.6; building them all for one would be a second distribution to keep.
- **The system's C library, built on Fedora 44.** Simpler, but it would run only where glibc is 2.43 or newer, where the packages are.
- **linuxdeploy and its Qt plugin.** They find Qt's own plugins and QML, not KDE Frameworks' (KIO's workers, Kirigami's styles, the KDE platform theme), and don't bundle the C library; the script would have had to do most of it anyway, beside two more downloaded tools.
- **PipeWire's client library and modules in the AppImage**, through `PIPEWIRE_MODULE_DIR` and `SPA_PLUGIN_DIR`. Every program Reverblabr opens would inherit them and load another version's modules.
- **Auto Run and timers through the AppImage's file** (`$APPIMAGE`, with the program chosen by name). They would break when the file is moved or deleted, from a unit that nothing in the window shows.
- **The engine in the window only** (`--in-process`), as for the Flatpak (ADR-0034): sessions would stop with the window, and the command-line tool would have nothing to reach.

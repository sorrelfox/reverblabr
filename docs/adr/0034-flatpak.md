<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0034: The Flatpak: one sandbox with the window, the engine and the plugin hosts; the window starts the engine; no Auto Run, timers, KRunner, panel widget or all-users helper

- **Status:** Accepted
- **Date:** 2026-11-04

## Context

Reverblabr should also come as a Flatpak, built on the KDE runtime and attached to a release as a bundle. Several of its parts lean on the host's session, which a sandbox does not give:

- **The engine is started by D-Bus activation** (ADR-0017), through a file in `/usr/share/dbus-1/services/` and a systemd user unit. Flatpak exports an app's activation files only for names under its own id, `io.github.reverblabr.Reverblabr.*`; the engine's name is `io.github.reverblabr.Engine`, which is the public API (ADR-0020) and does not change. A window in a sandbox finds no activation file for it, and would run its own engine and say the background one isn't installed.
- **Auto Run** (ADR-0021) enables the daemon's unit through the user's service manager. **Timers** write systemd timers into `~/.config/systemd/user/`. A sandbox has neither the manager on its bus nor the host's folder, and an engine inside it can't be started at login by anything.
- **KRunner** (ADR-0023) finds the runner through a file in `/usr/share/krunner/dbusplugins/`, and the **panel widget** (ADR-0024) is a Plasma package with a QML plugin. Plasma reads neither from an app's sandbox.
- **The all-users helper** (ADR-0013) is a KAuth helper started by the system bus as root, which no Flatpak can install.
- **Virtual devices** are PipeWire drop-ins in `$XDG_CONFIG_HOME/pipewire/pipewire.conf.d/`. In a sandbox `XDG_CONFIG_HOME` is the app's own folder, which the host's PipeWire never reads. An engine that read an empty drop-in there would take the person's own devices for none at all.
- **Plugins** are hosted in processes of their own (ADR-0014) and show their windows as X11 windows (ADR-0025, ADR-0028). Flathub's plugins come as extensions of `org.freedesktop.LinuxAudio.Plugins`, each in `/app/extensions/Plugins/<format>`.
- **The process tree is the sandbox's.** A process the window starts stays in the sandbox, with its rights, and keeps it alive after the window has gone, as a detached daemon does outside one.

## Decision

**The window, the engine and the plugin hosts run in one sandbox**, with the KDE runtime (6.11). Every library the runtime lacks or has too old (PipeWire 1.6, the LV2 libraries, SoX Resampler, SoundTouch, RNNoise, the CLAP and VST3 headers, QtKeychain) is built in the manifest from a pinned source. The Model block is left out: ONNX Runtime is not built there.

**The engine is still `reverblabrd`, and the window starts it.** When `/.flatpak-info` is there, a window that finds nobody owning `io.github.reverblabr.Engine` runs `reverblabrd` from its own folder, detached, and waits for the name as long as it would wait for an activation. It never asks the bus to start the engine: an activation file there belongs to a package installed beside the Flatpak, whose engine would run outside the sandbox. Everything after that is ADR-0017: the window talks to the daemon over the bus, the daemon outlives it, and exits on its own once it is idle. The sandbox may own the engine's name (`--own-name`), the window's notifier name, and `io.github.reverblabr.reverblabr`, the lower-case name KDBusService takes to keep one Reverblabr a desktop. `reverblabr-ctl` reaches the daemon as outside a sandbox, run with `flatpak run --command=reverblabr-ctl`. A daemon that does not take the name in time is `daemonRefused`, and the window runs its own engine and says so, as before.

**Not in the Flatpak:**
- **Auto Run and timers.** The daemon does not keep a unit or timers in step when it is in a Flatpak, and starts no session on its own. The sessions keep their settings. The Info tab and the Schedule tab say that Reverblabr from a Flatpak can't do this.
- **KRunner.** The runner's file, the activation file and the systemd unit are removed from the Flatpak; the daemon's runner object is still served, and nothing asks it.
- **The panel widget** is not built (`RB_BUILD_PLASMOID=OFF`).
- **The all-users helper** is not built (`RB_BUILD_AUTH_HELPER=OFF`). Adding a device offers only the person's own, and the All Users choice says why.

**Virtual devices are the host's.** In a Flatpak the drop-in is read from and written to the host's configuration folder, `$HOST_XDG_CONFIG_HOME`, which Flatpak sets when the host has `XDG_CONFIG_HOME`, or else `~/.config`, the folder PipeWire reads. The sandbox is given `xdg-config/pipewire` for it, and nothing else of `~/.config`.

**Plugins from the extension point.** The manifest declares `org.freedesktop.LinuxAudio.Plugins` (25.08, the KDE runtime's base) with the LADSPA, LV2, CLAP and VST3 folders merged, and points `LV2_PATH`, `LADSPA_PATH`, `CLAP_PATH` and `VST3_PATH` at them. The host's own plugins in `/usr/lib64` are not seen: they are built against the host's libraries, not the runtime's. **X11 is given in full** (`--socket=x11`), not as a fallback for Wayland: a plugin's window is an X11 window on a Wayland desktop too.

**The other rights, each for a reason:** the PipeWire socket (`xdg-run/pipewire-0`), Wayland, the GPU, the Music folder for recordings, the notification server for the daemon's notifications, the tray's watcher and KGlobalAccel for the tray and global shortcuts, KWallet for the Broadcast block's password, and the media players' names on the bus.

**The seams are tested without a Flatpak.** Whether a process is in one is a file the tests can name elsewhere; the drop-in's folder takes whether it is in one as an argument; the window's start of the daemon is a function the reach test gives the daemon just built, on a bus of its own. The window's notices follow `AppShell.sandboxed`, which the QML tests set.

## Consequences

- A Flatpak has the same engine as a package: sessions keep running with the window closed, and the command-line tool works.
- An engine started by a window is that sandbox's: a second `flatpak run` finds it on the bus and does not start another.
- **The Flatpak and a package share the engine's name.** With both installed, whichever engine takes the name first serves both windows: a package's engine started at login is reached by the Flatpak's window too, and the other way round.
- Auto Run, timers, KRunner, the panel widget and devices for all users need a package. The window says so where they appear, and `packaging/README.md` says so.
- Plugins installed on the host are not available in the Flatpak. Flathub's plugin extensions are.
- **After PipeWire restarts, the Flatpak's engine stays cut off from it:** the sandbox keeps the socket file it was started with, so the engine can't reconnect as a package's does, until `flatpak kill io.github.reverblabr.Reverblabr` and a new start, with its sessions started again.
- `--socket=x11` beside Wayland is wider than a Wayland-only app needs, for the plugins' windows.

## Alternatives considered

- **The engine in the window only** (`--in-process` in a Flatpak). The least code, but sessions would stop with the window, the command-line tool would have nothing to reach, and the window would always say the background engine isn't there.
- **Starting the daemon through the Flatpak portal** (`flatpak-spawn`), in a sandbox of its own. It outlives the window as well, and the portal is one more moving part; the window's own child already does.
- **Renaming the engine's bus name to the app id's prefix**, so that Flatpak exports an activation file for it. It would change the public API (ADR-0020) for one way of packaging.
- **`--talk-name=org.freedesktop.Flatpak` and `flatpak-spawn --host`** for systemd units and timers. That is a hole through the sandbox, to the whole host, for two features.
- **The Background portal** for Auto Run at login. Possible later; it would start the window, not the engine, and needs code of its own.

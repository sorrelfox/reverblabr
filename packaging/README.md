<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Packaging

Distribution packaging goes in this folder. The first will be an RPM spec for Fedora; packages for Debian-based distributions and a Flatpak come later, after the first release.

No packaging file writes the version itself. It comes from [`VERSION`](../VERSION): a spec or changelog template takes it as `@VERSION@`, filled in from that file or from the release tarball's name (`reverblabr-X.Y.Z`). Releases are made with `tools/release.sh` ([Making a release](../docs/development.md#making-a-release)).

This page lists what a package has to contain. `cmake --install build/release --prefix /usr` puts all of it in place, and `DESTDIR` works as usual.

## The desktop entry, the AppStream data and the icon

Each is installed with its translations merged in ([i18n.md](../docs/i18n.md)):

| File | Where |
|---|---|
| `io.github.reverblabr.Reverblabr.desktop` | `/usr/share/applications/` |
| `io.github.reverblabr.Reverblabr.metainfo.xml` | `/usr/share/metainfo/` |
| the icon, drawn for every size | `/usr/share/icons/hicolor/scalable/apps/io.github.reverblabr.Reverblabr.svg` |
| the icon, drawn for 16 px | `/usr/share/icons/hicolor/16x16/apps/io.github.reverblabr.Reverblabr.svg` |
| `reverblabr.notifyrc` | `/usr/share/knotifications6/` |

`desktop-file-validate` and `appstreamcli validate` accept both files; the tests `tools.desktop-entry` and `i18n.data-translations` check that. The screenshots in the AppStream data are the images in [`docs/images/`](../docs/images/), fetched from the `main` branch on GitHub. The entry's actions, New Session and All Sessions, run `reverblabr --new-session` and `reverblabr --sessions`; a running Reverblabr takes them over.

## The background engine

The window uses the engine in `reverblabrd` by default ([ADR-0017](../docs/adr/0017-engine-daemon.md)), and the session bus starts it on the first call. A package has to ship all three of these files, or every window falls back to an engine of its own and says the background engine isn't installed:

| File | Where | What |
|---|---|---|
| `reverblabrd` | `/usr/bin/reverblabrd` | the daemon |
| `io.github.reverblabr.Engine.service` | `/usr/share/dbus-1/services/` | D-Bus activation: `Exec=/usr/bin/reverblabrd`, `SystemdService=reverblabrd.service` |
| `reverblabrd.service` | `/usr/lib/systemd/user/` | the user unit, `Type=dbus` |

Nothing enables the unit except Auto Run ([ADR-0021](../docs/adr/0021-auto-run.md)): while any session is set to run at login, the daemon enables `reverblabrd.service` through the user service manager, and it disables it again once none is. A package should not enable it.

## The command-line tool

It goes with the engine, though a package may split it off; it talks to the engine over the bus and needs nothing else of Reverblabr:

| File | Where |
|---|---|
| `reverblabr-ctl` | `/usr/bin/reverblabr-ctl` |
| its man page | `/usr/share/man/man1/reverblabr-ctl.1` |
| bash completion | `/usr/share/bash-completion/completions/reverblabr-ctl` |
| zsh completion | `/usr/share/zsh/site-functions/_reverblabr-ctl` |

## The all-users helper

Built when KF6Auth is found. Its four files are the `authhelper` install component, listed in [docs/development.md](../docs/development.md#trying-the-all-users-helper). They are what lets Reverblabr make virtual devices for every user after asking for an administrator's password.

## A Flatpak

A Flatpak has no systemd user unit to hand. The daemon would have to run inside the sandbox, own `io.github.reverblabr.Engine` (`--own-name`), and be started by the window instead of by activation. That isn't done yet.

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Packaging

Distribution packaging goes in this folder. There is an RPM spec for Fedora, in [`rpm/`](rpm/); packages for Debian-based distributions and a Flatpak come later, after the first release.

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

## Fedora

[`rpm/reverblabr.spec.in`](rpm/reverblabr.spec.in) is the spec, as a template. [`rpm/srpm.sh`](rpm/srpm.sh) fills it in from a release tarball and makes the source RPM:

```sh
packaging/rpm/srpm.sh dist/reverblabr-0.1.0.tar.xz      # dist/reverblabr.spec and dist/reverblabr-0.1.0-1.*.src.rpm
sudo dnf builddep dist/reverblabr-*.src.rpm
rpmbuild --rebuild dist/reverblabr-*.src.rpm             # the packages, in ~/rpmbuild/RPMS
rpmlint -r packaging/rpm/reverblabr.rpmlintrc dist/reverblabr.spec dist/*.rpm ~/rpmbuild/RPMS/*/reverblabr-*.rpm
```

The script takes the spec from the tarball, not from the tree it runs in, so a package is always built the way its release says. `@VERSION@` comes from the tarball's name, and the changelog's date from the release's entry in the AppStream data, so one tarball always makes the same spec. The tarball is the one the release workflow makes, `git archive` with the prefix `reverblabr-X.Y.Z/`.

One package holds everything on this page, the all-users helper included. Its `%check` validates the desktop entry and the AppStream data and runs the tests of the engine's layers, which need nothing but the build. The tests that start a private PipeWire, a session bus or a window can't run in a build root, so they run in the project's CI and before every change instead.

[`rpm/install-check.sh`](rpm/install-check.sh) installs the packages in a throwaway Fedora container, runs every program's `--version`, checks the files above are where they belong and that the engine's unit is not enabled, and removes the package again:

```sh
podman run --rm -v ~/rpmbuild/RPMS:/rpms:Z -v "$PWD/packaging/rpm:/check:Z" \
    registry.fedoraproject.org/fedora:44 /check/install-check.sh /rpms
```

The release workflow does all of this from the release tarball and attaches the RPMs and the source RPM to the draft release. The CI's checks fill in the spec from every change and run rpmlint on it.

### COPR

COPR builds the source RPM for each Fedora and publishes the packages. Publishing a release on GitHub sends its source RPM there ([`.github/workflows/copr.yml`](../.github/workflows/copr.yml)), once the repository has the secret it needs. Until then that workflow says the secret is missing and does nothing. To switch it on:

1. Sign in at <https://copr.fedorainfracloud.org> with a Fedora account.
2. Make a project, `reverblabr`, with the chroots `fedora-44-x86_64` and `fedora-rawhide-x86_64` (and each newer Fedora as it comes out; `aarch64` as well, if wanted). The packages are built from the source RPM, so the project needs no source settings.
3. Copy the API configuration from <https://copr.fedorainfracloud.org/api/>: the whole file, `[copr-cli]` with `login`, `username`, `token` and `copr_url`. The token expires after 180 days; the same page renews it.
4. On GitHub, under the repository's Settings, Secrets and variables, Actions: a secret `COPR_CONFIG` with that file as its value. If the project is not the token owner's `reverblabr`, a variable `COPR_PROJECT` with `OWNER/PROJECT` as well.

For a release that was published before the secret was there, or whose COPR build failed for reasons of COPR's own, Actions, COPR, "Run workflow" with the release's tag sends it again. Once the packages are published, they are installed with:

```sh
sudo dnf copr enable OWNER/reverblabr
sudo dnf install reverblabr
```

## A Flatpak

A Flatpak has no systemd user unit to hand. The daemon would have to run inside the sandbox, own `io.github.reverblabr.Engine` (`--own-name`), and be started by the window instead of by activation. That isn't done yet.

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Packaging

Distribution packaging goes in this folder. There is an RPM spec for Fedora, in [`rpm/`](rpm/); packages for Debian-based distributions and a Flatpak come later, after the first release.

No packaging file writes the version itself. It comes from [`VERSION`](../VERSION): a spec or changelog template takes it as `@VERSION@`, filled in from that file or from the release tarball's name (`reverblabr-X.Y.Z`). Releases are made with `tools/release.sh` ([Making a release](../docs/development.md#making-a-release)).

This page lists what a package has to contain. `cmake --install build/release --prefix /usr` puts all of it in place, and `DESTDIR` works as usual.

## The desktop entry, the AppStream data, the icon and the handbook

Each is installed with its translations merged in ([i18n.md](../docs/i18n.md)):

| File | Where |
|---|---|
| `io.github.reverblabr.Reverblabr.desktop` | `/usr/share/applications/` |
| `io.github.reverblabr.Reverblabr.metainfo.xml` | `/usr/share/metainfo/` |
| the icon, drawn for every size | `/usr/share/icons/hicolor/scalable/apps/io.github.reverblabr.Reverblabr.svg` |
| the icon, drawn for 16 px | `/usr/share/icons/hicolor/16x16/apps/io.github.reverblabr.Reverblabr.svg` |
| `reverblabr.notifyrc` | `/usr/share/knotifications6/` |

`desktop-file-validate` and `appstreamcli validate` accept both files; the tests `tools.desktop-entry` and `i18n.data-translations` check that. The screenshots in the AppStream data are the images in [`docs/images/`](../docs/images/), fetched from the `main` branch on GitHub. The entry's actions, New Session and All Sessions, run `reverblabr --new-session` and `reverblabr --sessions`; a running Reverblabr takes them over.

The handbook, `index.cache.bz2` and `index.docbook` in `/usr/share/doc/HTML/en/reverblabr/`, is what Help → Handbook (F1) opens, as `help:/reverblabr`. It is built when KDocTools is found, and `-DRB_BUILD_HANDBOOK=ON` makes it a requirement, which a package should set. It belongs with the window, not in a separate documentation package: F1 has nothing to open without it.

## The background engine

The window uses the engine in `reverblabrd` by default ([ADR-0017](../docs/adr/0017-engine-daemon.md)), and the session bus starts it on the first call. A package has to ship all three of these files, or every window falls back to an engine of its own and says the background engine isn't installed:

| File | Where | What |
|---|---|---|
| `reverblabrd` | `/usr/bin/reverblabrd` | the daemon |
| `io.github.reverblabr.Engine.service` | `/usr/share/dbus-1/services/` | D-Bus activation: `Exec=/usr/bin/reverblabrd`, `SystemdService=reverblabrd.service` |
| `reverblabrd.service` | `/usr/lib/systemd/user/` | the user unit, `Type=dbus` |

**The sessions in KRunner** come with the engine ([ADR-0023](../docs/adr/0023-krunner-runner-in-the-daemon.md)): the daemon serves the runner itself, and KRunner finds it through one file, translated like the desktop entry. A package that ships the daemon ships this too:

| File | Where | What |
|---|---|---|
| `io.github.reverblabr.Runner.desktop` | `/usr/share/krunner/dbusplugins/` | `X-Plasma-DBusRunner-Service=io.github.reverblabr.Engine`, `X-Plasma-DBusRunner-Path=/io/github/reverblabr/Runner`; KRunner starts the daemon through the activation file above |

Its Open starts `/usr/bin/reverblabr --open-session=<id>`, so it expects the window in the same prefix as the daemon.

Nothing enables the unit except Auto Run ([ADR-0021](../docs/adr/0021-auto-run.md)): while any session is set to run at login, the daemon enables `reverblabrd.service` through the user service manager, and it disables it again once none is. A package should not enable it.

**Timers write units of their own, in the user's home.** For each timer that is on in the Schedule tab, the daemon writes `reverblabr-timer-<id>.timer` and `.service` into `~/.config/systemd/user/`, enables the timer and starts it; the service runs `reverblabr-ctl timer-fire --scheduled -- <id>` from the directory `reverblabrd` is installed in. A package ships none of these and should not touch them. So **`reverblabr-ctl` must be installed beside `reverblabrd`** for timers to work: a package that splits the tool off should make the daemon's package require it. A timer left behind after Reverblabr is removed fails to start its service, harmlessly; deleting the timer in Reverblabr first removes its units.

## The command-line tool

It goes with the engine, though a package may split it off; it talks to the engine over the bus and needs nothing else of Reverblabr:

| File | Where |
|---|---|
| `reverblabr-ctl` | `/usr/bin/reverblabr-ctl` |
| its man page | `/usr/share/man/man1/reverblabr-ctl.1` |
| bash completion | `/usr/share/bash-completion/completions/reverblabr-ctl` |
| zsh completion | `/usr/share/zsh/site-functions/_reverblabr-ctl` |

## The panel widget

Reverblabr Sessions, the Plasma panel widget ([ADR-0024](../docs/adr/0024-panel-widget.md)). It is built unless `RB_BUILD_PLASMOID` is off, and needs nothing of Plasma to build; Plasma loads it at run time. Its two parts go together:

| File | Where |
|---|---|
| the widget's package: `metadata.json` and `contents/ui/` | `/usr/share/plasma/plasmoids/io.github.reverblabr.sessions/` |
| its QML module: `qmldir`, `libreverblabr-plasmoidplugin.so` and its type description | `/usr/lib64/qt6/qml/io/github/reverblabr/plasmoid/` (Qt's QML folder; `/usr/lib/x86_64-linux-gnu/qt6/qml/` on Debian-based systems) |

The widget talks to the background engine, so it is no use without it. A package should not require Plasma for it: on another desktop the files simply sit unused. Its words are in a catalog of their own, `plasma_applet_io.github.reverblabr.sessions`, installed beside `reverblabr`'s once there are translations ([i18n.md](../docs/i18n.md)). `kpackagetool6 --appstream-metainfo` describes the widget for a software centre, if a distribution wants that.

## The plugin host

`reverblabr-plugin-host`, in the libexec folder (`/usr/libexec/` on Fedora), runs each plugin in a process of its own ([ADR-0014](../docs/adr/0014-plugin-host-process.md)) and describes plugins for the library. LV2 and LADSPA plugins are always hosted. **CLAP and VST3 plugins** are hosted when the build finds their SDKs: the CLAP SDK (`clap-devel`), and the VST3 SDK's interface headers, 3.8 or later (`vst3sdk-devel`; [ADR-0028](../docs/adr/0028-vst3-hosting.md)). Both are headers only, so the package needs neither at run time. `-DRB_BUILD_VST3=ON` makes the VST3 SDK a requirement, which a package should set: an SDK older than 3.8 is not under the MIT licence and is not used. A package description that names VST3 should say that VST is a registered trademark of Steinberg Media Technologies GmbH.

`reverblabr-model-host`, in the same folder, runs a Model block's model in a process of its own ([ADR-0031](../docs/adr/0031-model-block.md)). It is built when the build finds ONNX Runtime (`onnxruntime-devel`), and it is the only program that links it. `-DRB_BUILD_MODEL_BLOCK=ON` makes ONNX Runtime a requirement. No model is packaged.

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

One package holds everything on this page, the all-users helper and the panel widget included. Its `%check` validates the desktop entry and the AppStream data and runs the tests of the engine's layers, which need nothing but the build. The tests that start a private PipeWire, a session bus or a window can't run in a build root, so they run in the project's CI and before every change instead.

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

## Debian and Ubuntu

[`debian/`](debian/) is the source package's `debian/` folder, for Debian testing and Ubuntu 26.04; its `changelog` is a template, `changelog.in`. [`debian/build-deb.sh`](debian/build-deb.sh) builds the packages from a release tarball, in a plain `debian:testing` or `ubuntu:26.04` system:

```sh
podman run --rm -v "$PWD:/src:ro,z" -v "$PWD/dist:/dist:z" docker.io/library/debian:testing \
    /src/packaging/debian/build-deb.sh /dist/reverblabr-0.1.0.tar.xz /dist/debian
```

As `srpm.sh` does, the script takes the packaging from the tarball, the version from the tarball's name, and the changelog's date from the release's entry in the AppStream data. On Ubuntu the version gets `~ubuntu26.04`, so the two packages have different names and Debian's is the newer. As root the script installs the build tools and the build dependencies with apt; then it runs `dpkg-buildpackage` and lintian, which must report no errors. The folder gets the `.deb`, the source package (`.dsc`, `.orig.tar.xz`, `.debian.tar.xz`), the `.buildinfo` and the `.changes`.

One package holds everything on this page, as on Fedora. Its folders are Debian's: the plugin host and the model host are in `/usr/lib/x86_64-linux-gnu/libexec/`, the all-users helper where KAuth puts helpers (`/usr/lib/kf6/kauth/libexec/`), the panel widget's QML module in `/usr/lib/x86_64-linux-gnu/qt6/qml/`, and the zsh completion in `/usr/share/zsh/vendor-completions/`. The build runs the same tests as the RPM's `%check`, with link-time optimisation where the distribution turns it on (Ubuntu does, Debian does not), and the package does not enable the engine's unit.

The distributions do not package every library. A block whose library is missing, or too old, is left out through a build profile, which the script sets by itself:

| Profile | Leaves out | Debian testing | Ubuntu 26.04 |
|---|---|---|---|
| `pkg.reverblabr.nornnoise` | Speech Denoise (RNNoise) | built | left out: no `librnnoise-dev` |
| `pkg.reverblabr.nomodel` | the Model block (ONNX Runtime) | built | built |
| `pkg.reverblabr.novst3` | VST3 hosting (the VST3 SDK 3.8) | built | left out: 3.7 only |

CLAP hosting is off on both: neither packages the CLAP SDK.

[`debian/install-check.sh`](debian/install-check.sh) installs the package in a throwaway container of the same distribution, runs every program's `--version`, checks the files above are where they belong and that the engine's unit is not enabled, and purges the package again:

```sh
podman run --rm -v "$PWD/dist/debian:/debs:ro,z" -v "$PWD/packaging/debian:/check:ro,z" \
    docker.io/library/debian:testing /check/install-check.sh /debs
```

The release workflow does both for each distribution from the release tarball and attaches the two `.deb` files to the draft release (not the `-dbgsym` package).

## A Flatpak

[`flatpak/io.github.reverblabr.Reverblabr.yml`](flatpak/io.github.reverblabr.Reverblabr.yml) builds Reverblabr on the KDE runtime 6.11, sandboxed and reduced ([ADR-0034](../docs/adr/0034-flatpak.md)). The window, the engine and the plugin hosts run in one sandbox. The host's bus can't start the engine there, so the window starts `reverblabrd` itself, and the sandbox owns `io.github.reverblabr.Engine`. Everything the runtime lacks is built in the manifest from a pinned source: PipeWire 1.6, lilv and suil with the libraries they need, SoX Resampler, SoundTouch, RNNoise, the CLAP and VST3 headers and QtKeychain.

**Not in the Flatpak:** Auto Run, timers, KRunner, the panel widget, devices for all users and the Model block. The window says so where Auto Run, timers and devices for all users appear. The activation file, the systemd unit and the KRunner file are removed from it.

Plugins come from Flathub's extensions of `org.freedesktop.LinuxAudio.Plugins` (branch 25.08), in `/app/extensions/Plugins/`; plugins installed on the host are not seen. A plugin's own window is an X11 window, so the sandbox gets X11 beside Wayland. Virtual devices are written to the host's `~/.config/pipewire/`, the only part of `~/.config` it can reach. Recordings go to the Music folder.

[`flatpak/build-flatpak.sh`](flatpak/build-flatpak.sh) builds it, from this tree or from a release tarball, and makes the bundle. It needs `flatpak-builder` and Flathub as a remote; it installs the KDE SDK and runtime for the user when they are missing:

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
packaging/flatpak/build-flatpak.sh dist/reverblabr-0.1.0.tar.xz dist/flatpak   # dist/flatpak/reverblabr-0.1.0-x86_64.flatpak
packaging/flatpak/smoke-flatpak.sh dist/flatpak/reverblabr-0.1.0-x86_64.flatpak
```

[`flatpak/smoke-flatpak.sh`](flatpak/smoke-flatpak.sh) installs the bundle for the user, runs every program's `--version` in the sandbox, and opens the window offscreen for a few seconds on a session bus of its own, with no PipeWire socket, checking that the engine it starts takes its name. It removes the install again unless it was there before. The release workflow builds the bundle from the release tarball on a GitHub runner, runs this check, and attaches the bundle to the draft release.

The bundle is installed with `flatpak install --user reverblabr-0.1.0-x86_64.flatpak`; Flathub must be a remote, for the runtime. The command-line tool is `flatpak run --command=reverblabr-ctl io.github.reverblabr.Reverblabr`.

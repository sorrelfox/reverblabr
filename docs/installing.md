<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Installing Reverblabr

There are no packages yet. Until the first release the only way to get Reverblabr is to build it yourself, which takes a few commands and a few minutes.

The first packages will be RPMs for Fedora, because Fedora is what I use and test on. They will come from COPR, and once the first release is published there, installing is two commands (`dnf copr enable`, then `dnf install reverblabr`); this page will say the exact name then. Packages for Debian-based distributions and a Flatpak are planned too, but only after the first release.

## What you need

- **KDE Plasma 6 on Wayland.** Reverblabr is written for Plasma and nothing else. It may start on another desktop, but I don't try it there.
- **PipeWire 1.6 or newer**, with **WirePlumber 0.5 or newer**. Current distributions run both by default.
- **Fedora 44 or newer.** This is where Reverblabr is developed.
- **Kubuntu 26.04 LTS**, the secondary platform. It is the oldest system I stay compatible with (it decides the minimum versions of Qt and KDE Frameworks). Its builds and tests are checked in an Ubuntu 26.04 container, and CI checks them again before each release, but I rarely use it on a desktop, so it gets much less real use than Fedora.

## Build it

First install what the build needs. The package list lives in the repository, so clone it first:

```sh
git clone --branch dev https://github.com/sorrelfox/reverblabr.git
cd reverblabr
sudo dnf install $(sed 's/#.*//' tools/deps/fedora.txt)
```

The `dev` branch is where development happens; `main` only gets releases, and there hasn't been one yet. On Kubuntu 26.04 the list is `tools/deps/kubuntu.txt`, installed with `sudo apt install`. The lists include the tools the tests need. [What to install](dependencies.md) says what every package is for and which ones you can leave out.

`tools/check-deps.sh` checks what was found, with versions, and tells you what is missing.

Then build:

```sh
cmake --preset release
cmake --build --preset release
```

The tests are built too, which takes a while. If you only want the program, add `-DBUILD_TESTING=OFF` to the first command. Warnings stop the build. If your compiler is newer than mine and finds something new to warn about, add `--compile-no-warning-as-error` to the first command, and please tell me about the warning.

## Run it from the build

```sh
build/release/bin/reverblabr
```

Start it from a Plasma Wayland session. This is the quickest way to try it, and everything works except one thing: the background engine. Installed, Reverblabr's engine runs as a small service of its own (`reverblabrd`), so your sessions keep playing when you close the window. Run from the build tree, there is no such service, so the window runs the engine itself and a banner says so. Your sessions then stop when the window closes, and sessions can't start at login. `--in-process` starts it that way without the banner.

## Install it

To get everything, including the background engine, the launcher entry and the command-line tool, install the build:

```sh
sudo cmake --install build/release
```

This puts the files where a package would put them, under `/usr`. To remove them again:

```sh
sudo xargs rm -v < build/release/install_manifest.txt
```

Virtual devices you made stay after that, because they belong to PipeWire, not to Reverblabr. [Removing Reverblabr](virtual-devices.md#removing-reverblabr) says how to get rid of them.

To update, pull, build and install again:

```sh
git pull
cmake --build --preset release
sudo cmake --install build/release
```

## Virtual devices for all users

Reverblabr can make virtual devices for every account on the computer, which needs an administrator's password. That goes through a small helper that runs as root. A full install includes it. If you run Reverblabr from the build tree, you can install just the helper:

```sh
sudo cmake --install build/release --component authhelper
```

It is built only when KAuth is found (`kf6-kauth-devel` on Fedora). Without it, Reverblabr makes virtual devices for your own account only.

## Optional parts

A few parts are built only when their library is there. The build's summary says which ones it left out:

- **Speech Denoise** needs RNNoise (`rnnoise-devel`). Fedora packages it, Kubuntu 26.04 doesn't.
- **CLAP plugins** need the CLAP SDK (`clap-devel`). Fedora packages it, Kubuntu 26.04 doesn't.
- **Virtual devices for all users** need KAuth, as above.

The full list is in [What to install](dependencies.md#optional).

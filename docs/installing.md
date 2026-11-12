<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Installing Reverblabr

Reverblabr comes as a package for Fedora 44 and Fedora 45, for Debian testing and for Ubuntu 26.04 (Kubuntu included), and as a Flatpak and an AppImage for other distributions. All of them are on the [release page](https://github.com/sorrelfox/reverblabr/releases/latest), with the source. The commands below find a file by its ending, so keep one version of it in the folder you saved it in. You can also [build it yourself](#building-it-yourself), which takes a few commands and a few minutes.

## What you need

- **KDE Plasma 6 on Wayland.** Reverblabr is written for Plasma and nothing else. It may start on another desktop, but I don't try it there.
- **PipeWire 1.6 or newer**, with **WirePlumber 0.5 or newer**. Current distributions run both by default.
- **A 64-bit PC** (x86_64, also called amd64). The packages, the Flatpak and the AppImage are built for nothing else.

## Which one to take

| Your system | Take | It lacks |
|---|---|---|
| Fedora 44 or 45 | [the Fedora package](#fedora-44-and-45) | nothing |
| Debian testing | [the Debian package](#debian-testing) | CLAP plugins |
| Kubuntu 26.04, or Ubuntu 26.04 with Plasma | [the Ubuntu package](#ubuntu-and-kubuntu-2604) | CLAP and VST3 plugins, Speech Denoise and the voice model |
| Another distribution, or you'd like a sandbox | [the Flatpak](#the-flatpak) | Auto Run, timers, KRunner, the panel widget, devices for all users, the Model block, plugins installed on your system, and reconnecting after PipeWire restarts |
| Another distribution, without installing anything | [the AppImage](#the-appimage) | Auto Run, timers, KRunner, the panel widget and devices for all users |
| Anything else, or you want to work on Reverblabr | [your own build](#building-it-yourself) | whatever your system has no library for |

Use one of them: not a package, the Flatpak or the AppImage together ([below](#more-than-one-together)). [What each one has](#what-each-one-has) compares them in full.

## Fedora 44 and 45

The Fedora package has everything: every block, the background engine, Auto Run and timers, KRunner, the panel widget, virtual devices for all users, `reverblabr-ctl` with its manual page and shell completions, and the handbook.

There is one for each Fedora: the file ending in `.fc44.x86_64.rpm` is for Fedora 44, the one ending in `.fc45.x86_64.rpm` for Fedora 45. Take your Fedora's: a package built for one doesn't start on the other, since each is built against its own release's FFmpeg and Qt, whose libraries change from one Fedora to the next. Download it from the [release page](https://github.com/sorrelfox/reverblabr/releases/latest) and install it from the folder you saved it in. The command picks your Fedora's file by itself:

```sh
sudo dnf install ./reverblabr-*.fc$(rpm -E %fedora).x86_64.rpm
```

After an upgrade to a newer Fedora, install the newer Fedora's package the same way.

What it needs comes from Fedora's own repositories. It also recommends `openh264`, Cisco's H.264 encoder, for the Live Stream block's video; `layer-shell-qt`, for the windows that float over other programs; and `polkit`, for the password prompt of devices for all users.

Start Reverblabr from the application launcher, or with `reverblabr` in a terminal. To remove it:

```sh
sudo dnf remove reverblabr
```

**It is tied to Qt 6.11.** The window uses parts of Qt that Qt keeps private and changes from one version to the next, so the package requires Qt 6.11's. If your Fedora moves on to Qt 6.12, that requirement can't be met any more, and the package has to be rebuilt. The source RPM on the release page, the file ending in `.fc44.src.rpm`, does that, for a newer Qt or for another Fedora release. Remove the release's package first if it is installed: a rebuilt package has the same version, and dnf would take it for installed already. Your sessions and settings stay.

```sh
sudo dnf remove reverblabr
sudo dnf install rpm-build 'dnf-command(builddep)'
sudo dnf builddep ./reverblabr-*.fc44.src.rpm
rpmbuild --rebuild reverblabr-*.fc44.src.rpm
sudo dnf install ~/rpmbuild/RPMS/x86_64/reverblabr-[0-9]*.x86_64.rpm
```

The rebuild compiles everything and runs the tests, so it takes a while.

## Debian testing

The Debian package has everything but CLAP plugins: Debian has no package of the CLAP SDK, which hosting them needs.

Download the file ending in `-1_amd64.deb`, the `.deb` without `ubuntu` in its name, from the [release page](https://github.com/sorrelfox/reverblabr/releases/latest) and install it from the folder you saved it in:

```sh
sudo apt install ./reverblabr_*-1_amd64.deb
```

It recommends `polkitd`, for the password prompt of devices for all users, and `qml6-module-org-kde-layershell`, for the windows that float over other programs. Start Reverblabr from the application launcher, or with `reverblabr` in a terminal. To remove it:

```sh
sudo apt purge reverblabr
```

**It is tied to the Qt it was built with.** The window uses parts of Qt that Qt keeps private, so the package requires exactly the Qt version Debian testing had when the release was built. Testing moves on, and once its Qt does, that requirement can't be met any more and the package has to be rebuilt. [Packaging](../packaging/README.md#debian-and-ubuntu) shows how to build it from the release's source tarball.

## Ubuntu and Kubuntu 26.04

The Ubuntu package is for Kubuntu 26.04, and for Ubuntu 26.04 with Plasma. It lacks three parts, because Ubuntu 26.04 doesn't package what they need:

- **CLAP plugins.** Ubuntu has no package of the CLAP SDK.
- **VST3 plugins.** Ubuntu's VST3 SDK is version 3.7. Reverblabr takes 3.8 or later, the first version under the MIT licence.
- **Speech Denoise, and the voice model** that Speech Now, Voice Gauge and Night Level listen with. Ubuntu has no package of RNNoise. Those three blocks listen with **Level and rhythm** instead, a simpler detector that can be wrong more often.

Download the file ending in `.ubuntu26.04_amd64.deb` from the [release page](https://github.com/sorrelfox/reverblabr/releases/latest) and install it from the folder you saved it in:

```sh
sudo apt install ./reverblabr_*.ubuntu26.04_amd64.deb
```

Everything else is as on [Debian](#debian-testing): what it recommends, starting it, `sudo apt purge reverblabr` to remove it, and being tied to the Qt it was built with, here Ubuntu 26.04's.

## The Flatpak

The Flatpak is for any other distribution that has Flatpak, or for when you'd like Reverblabr in a sandbox. It runs on KDE's runtime from Flathub, so it doesn't depend on your distribution's libraries. The window, the background engine and the plugins all run in the one sandbox.

Download the file ending in `-x86_64.flatpak` from the [release page](https://github.com/sorrelfox/reverblabr/releases/latest) and install it for your account. Flathub has to be one of your remotes, because the runtime, `org.kde.Platform` 6.11, comes from there:

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./reverblabr-*-x86_64.flatpak
```

Start it from the application launcher, or with `flatpak run io.github.reverblabr.Reverblabr`. To remove it:

```sh
flatpak uninstall --user io.github.reverblabr.Reverblabr
```

Add `--delete-data` to remove its sessions, presets and settings as well. Recordings and virtual devices stay. The KDE runtime it brought stays too; `flatpak uninstall --user --unused` removes it once nothing else needs it.

The command-line tool is `flatpak run --command=reverblabr-ctl io.github.reverblabr.Reverblabr`. It reaches the background engine while one runs, but can't start one: the window starts the engine, and the engine stays while a session runs.

**What it lacks, and why:**

- **Auto Run at login, and timers.** Both go through your account's systemd, which a sandbox can't reach, and nothing outside the sandbox can start its engine. The Info and Schedule tabs say so.
- **KRunner and the panel widget.** Plasma looks for them in folders on your system, not inside a sandbox.
- **Virtual devices for all users.** They need a helper that runs as root, which a Flatpak can't install. Devices for your own account work as in a package.
- **The Model block.** The ONNX Runtime it needs isn't built into the Flatpak.
- **Plugins installed on your system.** They are built against your system's libraries, not the runtime's, so the sandbox doesn't look for them. It finds the plugins Flathub packages as extensions of `org.freedesktop.LinuxAudio.Plugins` (branch 25.08) instead.
- **The manual page and the shell completions** of `reverblabr-ctl`.
- **The handbook (F1).** It is inside the sandbox, where the desktop's help viewer most likely can't read it. The same pages are in the [documentation](README.md).
- **Coming back after PipeWire restarts.** The sandbox keeps the PipeWire socket it started with, and a restarted PipeWire makes a new one, so the Flatpak's engine can't reconnect by itself as a package's does; closing the window doesn't help. If your sessions stay silent after PipeWire restarted, run `flatpak kill io.github.reverblabr.Reverblabr`, start Reverblabr again and start your sessions again.

Flathub's plugins don't come with the Flatpak. Install the ones you want, LSP's for example, and **Look for plugins again** in the library finds them:

```sh
flatpak install --user flathub org.freedesktop.LinuxAudio.Plugins.LSP//25.08
```

**Languages.** Reverblabr's own words in every language are in the Flatpak. Qt's and KDE's (a dialog's standard buttons, a shortcut written out) come from the runtime's translations, which Flatpak installs for your system's languages. If you choose another language in Reverblabr's Settings, add it to them, German for example, so those words follow too:

```sh
flatpak config --user --set extra-languages de
flatpak update --user
```

The sandbox may write to your Music folder, where recordings go unless a Recorder names another folder; a folder elsewhere hasn't been tried in the Flatpak yet. It has had less use so far than the packages.

## The AppImage

The AppImage is one file that runs without being installed, on any other distribution with Plasma 6 and PipeWire 1.6. It brings everything it needs, Qt, KDE Frameworks and even its own C library, so it runs on distributions older than the ones the packages are for; it is built on Fedora 44, and each release's is started on Ubuntu 24.04 before it is published. Only your graphics drivers and PipeWire's library come from your system. It is no sandbox: it uses your folders, your virtual devices and your plugins as a package does.

Download the file ending in `-x86_64.AppImage` from the [release page](https://github.com/sorrelfox/reverblabr/releases/latest), put it where you keep programs, make it executable and start it:

```sh
chmod +x ./reverblabr-*-x86_64.AppImage
./reverblabr-*-x86_64.AppImage
```

It doesn't add itself to the application launcher; an AppImage manager, such as Gear Lever, does that for you. The window starts Reverblabr's background engine when none is running, and the engine keeps running, and the AppImage with it, after the window is closed, while a session runs. To remove it, delete the file. Your sessions, presets and settings are in your home folder, as with a package, and stay.

The command-line tool is the AppImage called by its name. Make a link named `reverblabr-ctl` to the file, somewhere on your `PATH`:

```sh
ln -s "$PWD"/reverblabr-*-x86_64.AppImage ~/.local/bin/reverblabr-ctl
```

It reaches the background engine while one runs, but can't start one.

An AppImage runs through FUSE, which most distributions have (`fusermount3`). Without it, unpack the AppImage once and start what it unpacked:

```sh
./reverblabr-*-x86_64.AppImage --appimage-extract
squashfs-root/AppRun
```

Not `--appimage-extract-and-run`: that deletes its copy when the window closes, while the engine is still running from it.

**What it lacks, and why:**

- **Auto Run at login, and timers.** Both would have your account's systemd start a program inside the AppImage later, and the AppImage is there only while it runs, wherever you keep the file. The Info and Schedule tabs say so.
- **KRunner and the panel widget.** Plasma looks for them in folders on your system, and the AppImage installs nothing there.
- **Virtual devices for all users.** They need a helper installed as root. Devices for your own account work as in a package.
- **The manual page and the shell completions** of `reverblabr-ctl`.

Plugins installed on your system run in the AppImage's plugin host, with its libraries; one built for a newer system than Fedora 44 may not load. The handbook (F1) is inside the AppImage, which your desktop's help viewer may not find; that hasn't been tried yet. The same pages are in the [documentation](README.md). The AppImage is the newest way to get Reverblabr, and the one that has had the least use so far.

## More than one together

Use one of them. The packages, the Flatpak and the AppImage give their background engine the same name on your desktop's bus, `io.github.reverblabr.Engine`. With two installed, whichever engine starts first serves both windows, so the Flatpak's or the AppImage's window can end up talking to the package's engine, or the other way round. The package and the AppImage keep their sessions in the same place; the Flatpak keeps its own.

## Checking a download

The release page has `SHA256SUMS` beside the files. In the folder you saved them in:

```sh
sha256sum -c --ignore-missing SHA256SUMS
```

Each file you downloaded should say `OK`, which means it arrived whole.

Where a file comes from is shown by its build provenance attestation: a signed record, kept by GitHub, that the file was built by this repository's release workflow. The tarball and each package have one. With the [GitHub CLI](https://cli.github.com), for each file you downloaded, here the Fedora package:

```sh
gh attestation verify ./reverblabr-*.fc$(rpm -E %fedora).x86_64.rpm --repo sorrelfox/reverblabr
```

`SHA256SUMS` has no attestation of its own, and the packages are not signed with a GPG key. GitHub's own *Source code* archives are not the release's tarball: they are not in `SHA256SUMS` and have no attestation.

## Updating

None of these update themselves: they don't come from a repository that dnf, apt or Flatpak follows. For a new version, download its file from the release page and install it the same way. For the Flatpak, add `--reinstall` to the install command. For the AppImage, put the new file in place of the old one.

## Removing Reverblabr

The sections above say how to remove each one. Two things stay behind on purpose:

- **Virtual devices** belong to PipeWire, which makes them again at every login. Delete them in the Virtual Devices window before you remove Reverblabr, or delete their files afterwards and log out and back in. [Removing Reverblabr](virtual-devices.md#removing-reverblabr) names the files.
- **Timers** are systemd user timers in your home folder (not in the Flatpak or the AppImage, which have none). Delete them in the Schedule tab first. A timer left behind fails each time it comes round, harmlessly.

## Building it yourself

Reverblabr builds on these systems:

- **Fedora 44 or newer.** This is where Reverblabr is developed.
- **Kubuntu 26.04 LTS**, the secondary platform. It is the oldest system I stay compatible with (it decides the minimum versions of Qt and KDE Frameworks). Its builds and tests are checked in an Ubuntu 26.04 container, and CI checks them again before each release, but I rarely use it on a desktop, so it gets much less real use than Fedora.
- **Debian testing**, with Kubuntu's package names and the libraries Kubuntu 26.04 lacks.

### Build it

First install what the build needs. The package list lives in the repository, so clone it first:

```sh
git clone --branch main https://github.com/sorrelfox/reverblabr.git
cd reverblabr
sudo dnf install $(sed 's/#.*//' tools/deps/fedora.txt)
```

`main` holds the latest release. The `dev` branch is where development happens: clone that one to try what comes next, or to work on Reverblabr. On Kubuntu 26.04 the list is `tools/deps/kubuntu.txt` and on Debian testing `tools/deps/debian.txt`, each installed with `sudo apt install`. The lists include the tools the tests need. [What to install](dependencies.md) says what every package is for and which ones you can leave out.

`tools/check-deps.sh` checks what was found, with versions, and tells you what is missing.

Then build:

```sh
cmake --preset release
cmake --build --preset release
```

The tests are built too, which takes a while. If you only want the program, add `-DBUILD_TESTING=OFF` to the first command. Warnings stop the build. If your compiler is newer than mine and finds something new to warn about, add `--compile-no-warning-as-error` to the first command, and please tell me about the warning.

### Run it from the build

```sh
build/release/bin/reverblabr
```

Start it from a Plasma Wayland session. This is the quickest way to try it, and everything works except one thing: the background engine. Installed, Reverblabr's engine runs as a small service of its own (`reverblabrd`), so your sessions keep playing when you close the window. Run from the build tree, there is no such service, so the window runs the engine itself and a banner says so. Your sessions then stop when the window closes, and sessions can't start at login. `--in-process` starts it that way without the banner.

### Install it

To get everything, including the background engine, the launcher entry and the command-line tool, install the build:

```sh
sudo cmake --install build/release
```

This puts the files where a package would put them, under `/usr`. Don't install a build over one of the packages: remove the package first. To remove the files again:

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

### Virtual devices for all users

Reverblabr can make virtual devices for every account on the computer, which needs an administrator's password. That goes through a small helper that runs as root. A full install includes it. If you run Reverblabr from the build tree, you can install just the helper:

```sh
sudo cmake --install build/release --component authhelper
```

It is built only when KAuth is found (`kf6-kauth-devel` on Fedora). Without it, Reverblabr makes virtual devices for your own account only.

### Optional parts

A few parts are built only when their library is there. The build's summary says which ones it left out:

- **Speech Denoise**, and the voice model of Speech Now, Voice Gauge and Night Level, need RNNoise (`rnnoise-devel`, `librnnoise-dev`). Fedora and Debian testing package it, Kubuntu 26.04 doesn't.
- **Model** needs ONNX Runtime (`onnxruntime-devel`, `libonnxruntime-dev`). Fedora, Debian testing and Kubuntu 26.04 package it.
- **CLAP plugins** need the CLAP SDK (`clap-devel`). Fedora packages it; Debian and Kubuntu 26.04 don't.
- **VST3® plugins** need the VST3 SDK 3.8 or later (`vst3sdk-devel`, `libvst3sdk-dev`). Fedora and Debian testing package it; Kubuntu 26.04 has only 3.7, which the build doesn't use. VST is a registered trademark of Steinberg Media Technologies GmbH.
- **Virtual devices for all users** need KAuth, as above.

The full list is in [What to install](dependencies.md#optional).

## What each one has

| | Fedora 44 and 45 | Debian testing | Ubuntu 26.04 | Flatpak | AppImage | Your own build |
|---|---|---|---|---|---|---|
| Every block not named below, recording, virtual devices for your account | yes | yes | yes | yes | yes | yes |
| Speech Denoise, and the voice model of Speech Now, Voice Gauge and Night Level | yes | yes | no | yes | yes | with RNNoise 0.2 |
| Model block (Beta) | yes | yes | yes | no | yes | with ONNX Runtime 1.20 or later |
| Broadcast and Live Stream | yes | yes | yes | yes | yes | with QtKeychain, and for Live Stream FreeType, HarfBuzz, fontconfig and libswscale |
| LV2 and LADSPA plugins | yes | yes | yes | Flathub's | yes | yes |
| CLAP plugins | yes | no | no | Flathub's | yes | with the CLAP SDK 1.2 |
| VST3 plugins | yes | yes | no | Flathub's | yes | with the VST3 SDK 3.8 or later |
| Plugins installed on your system | yes | yes | yes | no | yes, with the AppImage's libraries | yes |
| Sessions keep running with the window closed | yes | yes | yes | yes | yes | once installed |
| Sessions come back by themselves after PipeWire restarts | yes | yes | yes | no: `flatpak kill`, then start it again | yes | yes |
| `reverblabr-ctl` | yes | yes | yes | through `flatpak run`, without manual page and completions | through a link named `reverblabr-ctl`, without manual page and completions | once installed |
| Auto Run and timers | yes | yes | yes | no | no | once installed |
| KRunner and the panel widget | yes | yes | yes | no | no | once installed |
| Virtual devices for all users | yes | yes | yes | no | no | with KAuth, and the helper installed |
| The handbook (F1) | yes | yes | yes | most likely not | not tried | with KDocTools, once installed |
| Where recordings can go | anywhere | anywhere | anywhere | the Music folder | anywhere | anywhere |
| Built against | each Fedora's own Qt 6.11 | the Qt Debian testing had at the release | Ubuntu 26.04's Qt | KDE's runtime 6.11 from Flathub | Fedora 44's Qt 6.11, which it brings | your system |
| Updates | by hand | by hand | by hand | by hand | by hand | `git pull` and build again |

The handbook opens in KHelpCenter, which none of the packages installs for you.

## Known limitations

- **Plasma on Wayland only.** Plasma on X11 and other desktops are not tried.
- **Capturing an application, you hear it twice** until you move the application to an output you aren't listening to, or use a virtual device ([Getting started](getting-started.md#run-it)).
- **A connection that would feed a session back into itself is refused.** Name your real speakers in the Output Device ([When something goes wrong](troubleshooting.md#common-problems)).
- **Without real-time priority** from rtkit, dropouts are likelier.
- **Run from the build tree**, there is no background engine: sessions stop with the window, and there is no Auto Run, no timers, no KRunner and no panel widget.
- **Some blocks depend on the package or the build**, as the table above says.
- **A plugin that crashes** leaves its block bypassed until you restart it.
- **Virtual devices stay** after Reverblabr is removed, until you delete them.
- **Timers don't wake the computer.** A time that passed while it slept starts nothing, unless it wakes within two minutes of it. **Global shortcuts** end when Reverblabr is quit from the tray, even while sessions keep running.
- **Only one Reverblabr runs per desktop.** A build started from the source tree just raises an installed one that is running.
- **Live Stream needs an H.264 encoder** on the computer. On Fedora that is `openh264`, or the full FFmpeg from RPM Fusion. Which encoder the Flatpak finds hasn't been tried yet.
- **The windows that float over other programs** need `layer-shell-qt`. On X11 they stay above the main window only.
- **The Model block is Beta.** No models are included, Fedora's ONNX Runtime runs it on the processor only, and a voice adds about 390 ms of delay ([Blocks](blocks.md#model)).
- **The Hearing blocks are not a medical device**, and their limits are digital, not a level at your ear ([Hearing](hearing.md)). Frequency Raising is experimental in parts, and Sudden Sounds' thresholds are provisional. Speech Now, Voice Gauge and Sound Side can be wrong.
- **A running session isn't listed in the Plasma audio applet**, only in PipeWire's own tools ([Privacy](privacy.md)).
- **A Bluetooth headset** drops to hands-free sound while anything records from it.
- **The Flatpak** lacks what [its section](#the-flatpak) lists, shares the engine's name with the packages, and has to be killed and started again after PipeWire restarts.
- **The AppImage** lacks what [its section](#the-appimage) lists, shares the engine's name with the packages, and is about 150 MB.
- **Nothing updates itself.** Download each new version from the release page.

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

## Downloads

Take the file for your system and install it from the folder you saved it in. The packages and the Flatpak are for 64-bit PCs (x86_64, also called amd64).

**Fedora 44:** `reverblabr-@VERSION@-1.fc44.x86_64.rpm`

```sh
sudo dnf install ./reverblabr-@VERSION@-1.fc44.x86_64.rpm
```

**Ubuntu 26.04 and Kubuntu 26.04:** `reverblabr_@VERSION@-1.ubuntu26.04_amd64.deb`

```sh
sudo apt install ./reverblabr_@VERSION@-1.ubuntu26.04_amd64.deb
```

**Debian testing:** `reverblabr_@VERSION@-1_amd64.deb`

```sh
sudo apt install ./reverblabr_@VERSION@-1_amd64.deb
```

**Any other distribution, with Flatpak:** `reverblabr-@VERSION@-x86_64.flatpak`. The KDE runtime it runs on comes from Flathub:

```sh
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user ./reverblabr-@VERSION@-x86_64.flatpak
```

Install a package or the Flatpak, not both: the two would share one background engine.

### What each one lacks, and why

| | Lacks | Why |
|---|---|---|
| Fedora 44 | nothing | |
| Debian testing | CLAP plugins | Debian has no package of the CLAP SDK. |
| Ubuntu and Kubuntu 26.04 | CLAP plugins, VST3 plugins, Speech Denoise, and the voice model Speech Now, Voice Gauge and Night Level listen with (they use a simpler detector) | Ubuntu has no package of the CLAP SDK or of RNNoise, and its VST3 SDK, 3.7, is older than 3.8, the first version under the MIT licence. |
| Flatpak | Auto Run at login, timers, KRunner, the panel widget, virtual devices for all users, the Model block, plugins installed on your system, and reconnecting after PipeWire restarts | A sandbox can't reach the parts of the desktop these need, and keeps the PipeWire socket it started with. ONNX Runtime, which the Model block needs, isn't built into it. It sees only the plugins Flathub packages for it. |

The Fedora, Debian and Ubuntu packages are built against their distribution's Qt and have to be rebuilt when it moves to a newer one; the Flatpak brings its own. None of them updates itself. [Installing](https://github.com/sorrelfox/reverblabr/blob/v@VERSION@/docs/installing.md) has the details, and how to remove Reverblabr again.

### Checking a download

In the folder you saved the files in:

```sh
sha256sum -c --ignore-missing SHA256SUMS
gh attestation verify ./reverblabr-@VERSION@-1.fc44.x86_64.rpm --repo sorrelfox/reverblabr
```

The first says whether each file you have arrived whole. The second, with the GitHub CLI, checks one file's build provenance attestation: that it was built by this repository's release workflow. The tarball and every package have one. The packages are not signed with a GPG key.

### Source

`reverblabr-@VERSION@.tar.xz` is the source every package was built from, and `reverblabr-@VERSION@-1.fc44.src.rpm` rebuilds the Fedora package, for another Fedora release, for example. GitHub's own *Source code* archives are not the release's tarball: they are not in `SHA256SUMS` and have no attestation.

VST is a registered trademark of Steinberg Media Technologies GmbH.

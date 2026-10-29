<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# What to install

Reverblabr is built from source. This page lists every library and tool it uses, whether it is required, and what goes missing without each optional one. The package names are Fedora's and Kubuntu's. The same names are kept in [`tools/deps/fedora.txt`](../tools/deps/fedora.txt) and [`tools/deps/kubuntu.txt`](../tools/deps/kubuntu.txt), and a test checks that every package in those lists is described here.

## Everything at once

The lists install everything below, optional parts and development tools included.

**Fedora 44 or newer:**

```sh
sudo dnf install $(sed 's/#.*//' tools/deps/fedora.txt)
```

**Kubuntu 26.04:**

```sh
sudo apt install $(sed 's/#.*//' tools/deps/kubuntu.txt)
```

Then check what was found:

```sh
tools/check-deps.sh
```

It prints each library with the version it found, fails if something required is missing or too old, and only warns about optional ones.

## What has to be running

Reverblabr works with the audio system of the desktop you are logged in to:

- **KDE Plasma 6 on Wayland.**
- **PipeWire 1.6 or later**, with **WirePlumber 0.5 or later** as its session manager. Fedora and Kubuntu run both by default (`pipewire`, `wireplumber`).

## Required

Without any of these the build stops and says which one is missing.

### Build tools

| What | Fedora | Kubuntu | For |
|---|---|---|---|
| CMake, Ninja | `cmake`, `ninja-build` | `cmake`, `ninja-build` | the build |
| C++ compiler | `gcc-c++` | `g++` | the build (GCC or Clang) |
| Git | `git` | `git` | getting the source |
| pkg-config | `pkgconf-pkg-config` | `pkgconf` | finding the libraries below |
| KDE's CMake modules | `extra-cmake-modules` | `extra-cmake-modules` | the KDE parts of the build |
| gettext | `gettext` | `gettext` | the translation catalogs (`xgettext`, `msgfmt`) |
| GoogleTest | `gtest-devel` | `libgtest-dev` | the tests, which are built by default (`-DBUILD_TESTING=OFF` leaves them out, and then this is not needed) |
| Python 3 | `python3` | `python3` | the translation and tooling checks among the tests (not needed with `-DBUILD_TESTING=OFF`) |

### Qt 6

| What | Fedora | Kubuntu | For |
|---|---|---|---|
| Qt Base | `qt6-qtbase-devel` | `qt6-base-dev`, `qt6-base-dev-tools` | Core, Gui, Widgets, D-Bus, SQL, Test |
| Qt Declarative | `qt6-qtdeclarative-devel` | `qt6-declarative-dev`, `qt6-declarative-dev-tools` | the QML interface, and `qmllint` |
| Qt SVG | `qt6-qtsvg-devel` | `qt6-svg-dev` | the blocks' icons |
| Qt's SQLite driver | part of `qt6-qtbase` | `libqt6sql6-sqlite` | the recordings index in the Files tab |
| Qt Quick modules | part of `qt6-qtdeclarative` | `qml6-module-qtquick`, `qml6-module-qtquick-controls`, `qml6-module-qtquick-templates`, `qml6-module-qtquick-layouts`, `qml6-module-qtquick-window`, `qml6-module-qtquick-shapes`, `qml6-module-qtqml-workerscript`, `qml6-module-qttest` | the window at run time, and the QML tests |

### KDE Frameworks 6

| What | Fedora | Kubuntu | For |
|---|---|---|---|
| Kirigami | `kf6-kirigami-devel` | `libkirigami-dev`, `qml6-module-org-kde-kirigami` | the interface's building blocks |
| KI18n | `kf6-ki18n-devel` | `libkf6i18n-dev`, `qml6-module-org-kde-ki18n` | translations |
| KCoreAddons | `kf6-kcoreaddons-devel` | `libkf6coreaddons-dev`, `qml6-module-org-kde-coreaddons` | the About data |
| KConfig | `kf6-kconfig-devel` | `libkf6config-dev` | remembered settings: the appearance, recent applications, the window's size |
| KWindowSystem | `kf6-kwindowsystem-devel` | `libkf6windowsystem-dev` | the frameless window's shadow |
| KIO | `kf6-kio-devel` | `libkf6kio-dev` | the Files tab's Open Containing Folder, Open With… and Move to Trash |
| KGuiAddons | `kf6-kguiaddons-devel` | `libkf6guiaddons-dev` | the Files tab |
| KNotifications | `kf6-knotifications-devel` | `libkf6notifications-dev` | desktop notifications: a recording finished, a device gone, a plugin stopped, each one an event you can turn off in System Settings → Notifications |
| KStatusNotifierItem | `kf6-kstatusnotifieritem-devel` | `libkf6statusnotifieritem-dev` | the icon in the system tray: its popover with every session's Run/Stop and level, the meters marked "Show in system tray", a red dot while recording |
| KGlobalAccel | `kf6-kglobalaccel-devel` | `libkf6globalaccel-dev` | global shortcuts that work while Reverblabr is in the background: run or stop a session, bring the window forward, an Input Switch, a Time Shift, a Recorder's next file; set in System Settings → Shortcuts |
| KDBusAddons | `kf6-kdbusaddons-devel` | `libkf6dbusaddons-dev` | one Reverblabr a desktop: launching it again, or choosing New Session or All Sessions from the launcher's menu for it, brings the running one forward instead of starting a second |
| The Plasma style for Qt Quick | `kf6-qqc2-desktop-style` | `qml6-module-org-kde-desktop` | controls that look like the rest of the desktop |

### Audio and files

| What | Fedora | Kubuntu | For |
|---|---|---|---|
| PipeWire | `pipewire-devel` | `libpipewire-0.3-dev` | everything the app hears and plays |
| libsndfile | `libsndfile-devel` | `libsndfile1-dev` | recordings as WAV, AIFF, FLAC, MP3, Opus and Vorbis |
| SoX Resampler | `soxr-devel` | `libsoxr-dev` | a recording at another rate than its session |
| SoundTouch | `soundtouch-devel` | `libsoundtouch-dev` | the **Pitch Shift** block: the pitch moved without the speed changing |
| TagLib | `taglib-devel` | `libtag-dev` | a recording's tags |
| FFmpeg | `libavformat-free-devel`, `libavcodec-free-devel`, `libavutil-free-devel` | `libavformat-dev`, `libavcodec-dev`, `libavutil-dev` | M4A recordings (AAC and Apple Lossless), and reading them back |
| lilv and LV2 | `lilv-devel`, `lv2-devel` | `liblilv-dev`, `lv2-dev` | finding, describing and running LV2 plugins |

## Optional

The build finds these by itself and says in its summary which parts it could not build. Nothing else changes without them.

| What | Fedora | Kubuntu | Gives you | Without it |
|---|---|---|---|---|
| RNNoise | `rnnoise-devel` | not packaged in 26.04 | the **Speech Denoise** block | that block is not in the library. `-DRB_BUILD_SPEECH_DENOISE=OFF` leaves it out on purpose ([ADR-0016](adr/0016-rnnoise-optional.md)) |
| The CLAP SDK | `clap-devel` | not packaged in 26.04 | **CLAP plugins**: the CLAP block, and CLAP plugins in the library | no CLAP block, and CLAP plugins are not looked for. `-DRB_BUILD_CLAP=OFF` leaves them out on purpose |
| KAuth and polkit | `kf6-kauth-devel`, `polkit` | `libkf6auth-dev`, `polkitd` | **virtual devices for all users**, after the administrator's password | virtual devices are for your own account only. The password prompt also needs the helper installed once; see [Installing](installing.md#virtual-devices-for-all-users) |
| KDocTools | `kf6-kdoctools-devel` | `libkf6doctools-dev` | the **handbook** that Help → Handbook (F1) opens in KHelpCenter, and the test that checks it is valid DocBook | no handbook is built or installed, and that test is skipped. `-DRB_BUILD_HANDBOOK=OFF` leaves it out on purpose |
| Qt's Wayland client internals | `qt6-qtbase-private-devel` | `qt6-base-private-dev` | the **compositor's own window menu** on the title bar | the title bar shows Reverblabr's own menu instead |
| Qt ShaderTools | `qt6-qtshadertools-devel` | `qt6-shadertools-dev` | nothing yet: listed for shader effects the interface may use | nothing is lost today |

## Plugins

The plugin blocks host plugins that are already on your machine. Install the plugins you want to use; the block library's plugin row finds them, and **Look for plugins again** finds new ones.

- **LV2** plugins are looked for in `LV2_PATH` when it is set, otherwise in `~/.lv2`, `/usr/local/lib64/lv2`, `/usr/local/lib/lv2`, `/usr/lib64/lv2` and `/usr/lib/lv2`.
- **CLAP** plugins, where the build has the CLAP SDK (see [Optional](#optional)), are `.clap` files anywhere under `~/.clap`, `/usr/local/lib64/clap`, `/usr/local/lib/clap`, `/usr/lib64/clap` and `/usr/lib/clap`, and under the folders `CLAP_PATH` names. Unlike `LV2_PATH`, `CLAP_PATH` adds to those folders rather than replacing them, as the CLAP specification says.
- **LADSPA** plugins are looked for in `LADSPA_PATH` when it is set, otherwise in `~/.ladspa`, `/usr/local/lib64/ladspa`, `/usr/local/lib/ladspa`, `/usr/lib64/ladspa` and `/usr/lib/ladspa`. Nothing has to be installed to build LADSPA hosting: the plugin host loads each library itself. LSP comes as LADSPA too (`lsp-plugins-ladspa`), and many older collections are LADSPA only (on Fedora, the `ladspa-*-plugins` packages).
- **LSP** is a good first set: equalisers, compressors, gates and more, as LV2 (`lsp-plugins-lv2` on both) and as CLAP (`lsp-plugins-clap` on both). The tests check plugin hosting against them.

## Only for development and tests

None of these are needed to use Reverblabr. They are what [Development](development.md) and the tests use.

| What | Fedora | Kubuntu | For |
|---|---|---|---|
| A real locale | `glibc-langpack-en` | (installed) | the translation tests; without a locale gettext loads no catalogs |
| KDE's platform theme, DejaVu Sans | `plasma-integration`, `dejavu-sans-fonts` | (installed) | the pseudo-locale check, which measures text as a Plasma desktop draws it; DejaVu Sans draws the `⟦ ⟧` that Noto Sans has no glyphs for |
| Plasma's QML controls, kpackagetool6 | `libplasma` | (installed) | the panel widget's tests, which draw it with Plasma's controls and check its package with `kpackagetool6`; without them the representations' test is left out and the package is checked without `kpackagetool6` |
| Clang and its sanitizer runtimes | `clang`, `llvm`, `compiler-rt` | `clang`, `llvm`, `libclang-rt-dev` | the `rtsan` preset |
| GCC's sanitizer runtimes | `libasan`, `libubsan`, `libtsan` | part of `g++` | the `asan` preset |
| clang-format, clang-tidy | `clang-tools-extra` | `clang-format`, `clang-tidy` | the format check and lint |
| REUSE | `reuse` | `reuse` | the licence check |
| The desktop entry and AppStream validators | `desktop-file-utils`, `appstream` | `desktop-file-utils`, `appstream` | `desktop-file-validate` and `appstreamcli validate` in the tests, as a distribution runs them; without them the test says so and skips them |
| PipeWire's tools | `pipewire-utils`, `wireplumber` | `pipewire-bin`, `wireplumber` | `pw-top`, `pw-dump`, `pw-link`, `wpctl` and the rest, for you and for the tests |
| A graph view | `qpwgraph` | `qpwgraph` | seeing the PipeWire graph while you work |
| jq | `jq` | `jq` | querying `pw-dump` |
| The tests' own PipeWire | `pipewire`, `pipewire-pulseaudio`, `pulseaudio-utils`, `dbus-daemon` | `pipewire`, `pipewire-pulse`, `pulseaudio-utils`, `dbus` | the private PipeWire, Pulse client and D-Bus session the integration tests run in |
| libcanberra | `libcanberra` | `libcanberra0` | a test that plays a notification sound the way KDE does |
| LSP's LV2 plugins | `lsp-plugins-lv2` | `lsp-plugins-lv2` | the plugin hosting tests |
| The LADSPA SDK's example plugins | `ladspa` | `ladspa-sdk` | the LADSPA hosting tests' common plugin, the stereo amplifier; without it those checks are skipped, and the rest run against LADSPA plugins the tests build |
| LSP's CLAP plugins | `lsp-plugins-clap` | `lsp-plugins-clap` | the CLAP hosting tests, where the build has the CLAP SDK |

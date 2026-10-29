<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Credits and third-party components

Reverblabr is built on the work of many other projects. This file lists every third-party component the application uses, bundles or downloads, with its licence. Update it in the same change that adds a dependency.

## Libraries used at run time

These are system libraries, linked dynamically. None of their code is copied into this repository.

| Component | Used for | Licence |
|---|---|---|
| [Qt 6](https://www.qt.io) | application framework, user interface, D-Bus | LGPL-3.0-only (also available under GPL and commercial terms) |
| [KDE Frameworks 6](https://develop.kde.org/products/frameworks/): Kirigami, KI18n, KCoreAddons | application shell, translations, application metadata | LGPL-2.0-or-later and compatible licences |
| [qqc2-desktop-style](https://invent.kde.org/frameworks/qqc2-desktop-style) | native Breeze controls | LGPL-2.0-or-later and compatible licences |
| [PipeWire](https://pipewire.org) (libpipewire, SPA) | audio server client | MIT |
| [SoundTouch](https://www.surina.net/soundtouch/) | the Pitch Shift block | LGPL-2.1-or-later |

## Build and test tools

| Component | Used for | Licence |
|---|---|---|
| [extra-cmake-modules](https://invent.kde.org/frameworks/extra-cmake-modules) | CMake modules | BSD-3-Clause |
| [GoogleTest](https://github.com/google/googletest) | unit tests (not shipped) | BSD-3-Clause |

## Bundled components

None yet. Vendored sources will be listed here with their version and licence.

## Downloaded data

None yet. Downloadable data, such as machine-learning model weights, will be listed here with its licence and where its checksums come from.

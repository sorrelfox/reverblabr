<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0004: KDE Plasma on Wayland only; Fedora primary, Kubuntu secondary

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

Supporting every Linux desktop is expensive. Each one needs its own fallbacks for the tray, global shortcuts, notifications, window decorations and portals, and a much larger test matrix. The interface design is built on KDE's Breeze style.

Both candidate distributions ship Plasma 6.6, on Wayland only for Kubuntu 26.04 and by default for Fedora 44. The Plasma X11 session is being retired.

## Decision

- **Supported desktop:** KDE Plasma on **Wayland**. The Plasma X11 session and other desktops are not supported. The app may start there, but nothing is tested.
- **Platforms:**
  - **Fedora 44+ KDE Plasma** is the **primary** platform: development happens on it and it gets the most testing. The two latest Fedora releases are supported.
  - **Kubuntu 26.04 LTS** (Ubuntu with Plasma, LTS releases only) is **secondary**, with lower priority.
- **Minimum versions come from the oldest supported platform**, Kubuntu 26.04:

| Component | Minimum |
|---|---|
| Qt | 6.10 |
| KDE Frameworks | 6.24 |
| PipeWire | 1.6 |
| WirePlumber | 0.5 |
| Compiler | GCC 15 or Clang 21 |
| CMake | 3.24 |

- **KDE Frameworks are used in the GUI only.** The engine stays free of them ([ADR-0003](0003-in-process-engine-behind-facade.md)).
- **Windows use KWin's server-side decorations**; the app draws no title bar of its own. *Amended by [ADR-0012](0012-reverblabr-look.md): this holds for the Follow system appearance; the Reverblabr appearances draw their own.*

## Consequences

- Native integration without fallbacks: the tray (KStatusNotifierItem), notifications, global shortcuts, KWallet and crash reporting.
- A small test matrix: Fedora and Kubuntu, both on Plasma Wayland.
- Users of other desktops are not served. GNOME users would have to install Plasma.
- **Code must build against the Kubuntu floor while developers work on newer Fedora.** Using an API newer than Qt 6.10 or KF 6.24 breaks the secondary platform. The guards against this:
  - `tools/container-check.sh ubuntu:26.04 test`
  - a Kubuntu CI job (later)
  - the "since" notes in the API docs
- Plugin editors that only support X11 open through XWayland from the plugin host process.

## Alternatives considered

- **Several desktops (GNOME, Xfce and others).** Roughly four to six extra weeks of fallback work, plus a permanent test burden.
- **Ubuntu as the primary platform.** Development happens on Fedora, and Fedora gets new Qt, KDE Frameworks and PipeWire releases first, so problems surface there earlier.

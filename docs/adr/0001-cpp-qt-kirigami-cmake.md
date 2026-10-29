<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0001: C++23, Qt 6 Quick with Kirigami, CMake with ECM

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

The language and toolkit have to cover all of these at once:

- **Hard real-time DSP.** Code on PipeWire's real-time thread must never allocate, lock or make system calls, and its CPU time must be bounded.
- **The PipeWire C API.** We need all of it: filter nodes, the registry, the link factory, metadata and SPA parameter objects.
- **C and C++ libraries.** Plugin standards (LV2, CLAP, LADSPA, VST3), codecs, tagging and machine-learning runtimes all ship C or C++ APIs.
- **A custom canvas.** Tiles, wires and dozens of live meters at 60 frames per second, drawn on the GPU.
- **A native KDE look and feel.** Breeze controls, the user's colour scheme, KDE's desktop services.

## Decision

- **C++23** throughout, compiled with GCC 15+ or Clang 21+.
- **Qt 6** for the application:
  - Qt Quick (QML) for the user interface, with Kirigami for the shell and the native Breeze controls (`org.kde.desktop`) *(amended by [ADR-0012](0012-reverblabr-look.md): the Breeze controls are the Follow system appearance; the Reverblabr appearances use our own control style)*
  - QtCore, and later QtDBus, in the engine
- **CMake** with **extra-cmake-modules (ECM)**: KDE's install directories, compiler settings and QML module helpers.
- **GoogleTest** for C++ tests and **Qt Quick Test** for QML tests.

## Consequences

- Every C API is reachable directly, with no bindings or bridge layers, and the engine and GUI share one language.
- C++ offers no memory safety, so safety comes from tooling and process:
  - ASan, UBSan and TSan presets
  - Clang's RealtimeSanitizer on real-time paths
  - fuzzing of file loaders (later)
  - running third-party plugins out of process
- Code must stay within the Qt 6.10 and KDE Frameworks 6.24 APIs, the oldest supported platform ([ADR-0004](0004-kde-plasma-wayland-platforms.md)), even though development happens on newer versions.
- KDE's compiler settings are adopted: strict warnings, no implicit string conversions, and no Qt keywords (`Q_SIGNALS` / `Q_SLOTS` / `Q_EMIT` instead).
- **Exceptions stay enabled**, since the standard library can throw. Our own code reports errors as values ([conventions](../conventions.md#errors)).

## Alternatives considered

- **Rust engine with the same QML interface through CXX-Qt.** It gives memory safety, but it puts a bridge on the real-time path and between engine and GUI. Plugin-hosting libraries are younger, and we estimated about 15–20 % more schedule. Most of the risky code, such as plugins and codecs, would stay C or C++ anyway.
- **Rust with GTK 4 and libadwaita.** Its look conflicts with the Breeze-based design, and the canvas would be all custom widgets.
- **Slint.** A capable custom look, but a thinner widget set and less mature accessibility.
- **A web user interface (Tauri or Electron) over a native engine.** Higher memory use and latency, a non-native feel, and an extra IPC bridge.

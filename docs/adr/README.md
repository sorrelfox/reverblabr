<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Architecture decision records

Each record captures one significant decision: the situation, what was decided, and what follows from it. Records are numbered and never renumbered. To change a decision, write a new record that supersedes the old one, and set the old one's status to "Superseded by ADR-NNNN".

Start a new record from [the template](template.md).

| ADR | Decision | Status |
|---|---|---|
| [0001](0001-cpp-qt-kirigami-cmake.md) | C++23, Qt 6 Quick with Kirigami, CMake with ECM | Accepted |
| [0002](0002-native-pipewire-and-wireplumber-as-peer.md) | Native libpipewire; WirePlumber as the policy peer; fan-out links only | Accepted |
| [0003](0003-in-process-engine-behind-facade.md) | In-process engine first, behind a D-Bus-shaped facade | Accepted |
| [0004](0004-kde-plasma-wayland-platforms.md) | KDE Plasma on Wayland only; Fedora primary, Kubuntu secondary | Accepted |
| [0005](0005-licence-map.md) | MPL-2.0 licence map, REUSE and DCO | Accepted |
| [0006](0006-application-id-and-naming.md) | Application ID `io.github.reverblabr.Reverblabr` and naming scheme | Accepted |
| [0007](0007-capture-and-linking.md) | Capture and linking: port-driven links keyed by serial, per-stream System Audio, re-play and alert rules, node class | Accepted |
| [0008](0008-filter-runtime.md) | Filter-node runtime: always-process, 8192-frame buffers, graph handover by blocking invoke, PipeWire loop on the Qt event loop | Accepted |
| [0009](0009-canvas-rendering.md) | Canvas rendering: one ticker, meters as scene-graph items, Shape with CurveRenderer, a budgeted wire dash | Accepted |
| [0010](0010-real-time-communication-primitives.md) | Our own lock-free primitives in a new bottom layer `engine/rt` | Accepted |
| [0011](0011-canvas-block-list-model.md) | The canvas reads the session through a C++ list model | Accepted |
| [0012](0012-reverblabr-look.md) | The Reverblabr look: our own control style, the design's tokens and our own title bar, with *Follow system* kept as an appearance | Accepted |
| [0013](0013-virtual-devices.md) | Virtual devices as generated PipeWire drop-ins, mirrored by live nodes; all-users devices through a KAuth helper | Accepted |
| [0014](0014-plugin-host-process.md) | Plugins in host processes of their own, as PipeWire nodes the session is split around; parameters through shared memory, a heartbeat watchdog, bypass by relinking | Accepted |
| [0015](0015-own-fft-and-denoise.md) | Our own real FFT, and our own spectral denoiser rather than libspecbleach (whose FFTW is GPL) | Accepted |
| [0016](0016-rnnoise-optional.md) | Speech Denoise on the distribution's RNNoise, built only where there is one (Fedora, not Kubuntu 26.04) | Accepted |
| [0017](0017-engine-daemon.md) | The engine in a daemon, `reverblabrd`, reached over D-Bus and started on demand | Accepted |
| [0018](0018-telemetry-in-shared-memory.md) | A session's telemetry in a sealed memfd, handed to a window once, read-only, beside the facade | Accepted |
| [0019](0019-hearing-filterbank.md) | One 64-channel gammatone filterbank for both Hearing Compensation blocks, 4 ms, with the 16 bands' gains interpolated over it | Accepted, amended when Hearing Compensation 16 was built |
| [0020](0020-public-dbus-api.md) | The engine's interface is the public API: `io.github.reverblabr.Engine1`, only ever added to, its released surface pinned and documented | Accepted |
| [0021](0021-auto-run.md) | Auto Run is a session setting; the background engine starts those sessions, and keeps its unit enabled while there are any | Accepted, amended by 0023 (once a login) |
| [0022](0022-latency-that-follows-a-setting.md) | A block's settings may say what it is prepared for; the runtime prepares a fresh block when they change, and its lanes wait until it is filled | Accepted |
| [0023](0023-krunner-runner-in-the-daemon.md) | The sessions in KRunner: a D-Bus runner served by `reverblabrd` on an object of its own, not a separate helper or a plugin; Auto Run once a login and after a crash | Accepted |
| [0024](0024-panel-widget.md) | The panel widget is a Plasma package with a small QML plugin: sessions through the public API, levels through the telemetry handshake, calls on a connection of its own | Accepted |
| [0025](0025-plugin-windows.md) | A plugin's own window is shown by its host as an X11 window (an LV2 UI through suil, a CLAP window floating or put in one); what is done in it comes back as messages, never sent back to the host; a reported gesture is one undo step; presets are the format's own | Accepted |
| [0026](0026-broadcast-streaming.md) | Broadcast: our own source client, FFmpeg's encoders, and the password in the desktop's secret store | Accepted |

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0018: A session's telemetry in a sealed memfd, handed to a window once

- **Status:** Accepted
- **Date:** 2026-09-28

## Context

A window on the daemon (ADR-0017) had no levels: no meters, no wire colours, no state dots. The facade leaves telemetry out on purpose (ADR-0003). Sixty snapshots a second for every open session is bulk data, and it would cost messages whether anybody was looking or not. In process, the window reads a pointer to the session's `TelemetrySlot` on its own frame tick. The slot is an `rt::SeqlockValue<TelemetrySnapshot>`, and the snapshot was laid out to move into shared memory.

What there is to work with:
- **The seqlock is already fit for two processes.** It is a sequence number and an array of 32-bit words, and every one is a `std::atomic<std::uint32_t>`. Those are always lock-free, so they mean the same thing mapped at two addresses. The snapshot is trivially copyable and owns nothing.
- **The plugin host already shares memory with the engine** (ADR-0014): a memfd, created by the engine and handed to the host.
- **D-Bus carries file descriptors** (type `h`), and Qt passes them on a Unix transport.
- **The aim** is meters that behave the same on the daemon as in process, with no D-Bus message per frame (checked with `dbus-monitor`) and no leaks across reconnects.

## Decision

**Every open session publishes into shared memory, in either mode.** `core::SharedTelemetry` is a `TelemetryRegion` in a memfd:
- a header holds a magic number, a version and a fingerprint of the snapshot's layout (its size, its blocks' size, the offset of the blocks, and every bound that shapes it);
- the seqlock follows the header, unchanged.

`core::PublishedSlot` is what the sampler and the mock publish into. It uses the shared region, or a slot of the process's own only if the kernel refuses a memfd. The engine in the window reads the very same memory through a pointer, so in-process meters test the bytes a daemon window reads.

**The engine's memory is sealed at its size** (`F_SEAL_SHRINK`, `F_SEAL_GROW`, `F_SEAL_SEAL`) before anybody else can see it. No mapping of it can ever reach past the end of the file, which would be a SIGBUS in the window rather than an error.

**Handed over once, read-only, beside the facade.** The daemon serves `io.github.reverblabr.Telemetry` on the engine's object:
- `Share(sessionId) → h` returns a descriptor for the session's memory, or `io.github.reverblabr.Error.NoTelemetry` when no such session is open.
- The descriptor is **reopened read-only** through `/proc/self/fd`, not duplicated, so a window can neither write the memory nor map it for writing.
- It is the one method on the bus that returns a value, because a descriptor must go only to the window that asked.
- It lives in its own XML (`dbus/io.github.reverblabr.Telemetry.xml`), apart from the engine's API, which stays free of telemetry.

**The window maps once and is told when that changes.**
- `bus::RemoteEngine::telemetryOf(sessionId)` answers the mapping, or null while there is none.
- The first question about a session this window has open sends one `Share()`, without waiting.
- The mapping is checked (sealed, long enough, same magic, version and layout) and mapped read-only, and the descriptor is closed.
- `telemetryChanged(sessionId)` says when a mapping arrives. It says so again **before** a mapping goes: when the session closes, when the daemon leaves the bus, or when it hands the name to another.
- The window passes this on as `AppShell::telemetryMoved()`. `Meters`, the one reader that keeps the address, reads afresh, so no reader holds an address that has gone.
- A daemon that opens the session again is asked again. A `Share()` never starts a daemon: a new one would not have the session.

## Consequences

- **Nothing about levels crosses the bus while a session runs.** `bus.engine` watches a mock daemon publishing sixty snapshots a second with `dbus-monitor --profile` while a window reads every 16 ms, and sees no message. A window on the daemon has meters, wire colours and state dots, so the daemon can become the default.
- **A window that goes takes its mappings with it**, and the daemon keeps no descriptor it has handed out. Ten windows mapping and closing leave the descriptor and mapping counts where they started, on both sides.
- **The layout is a contract between two builds.** A change to the snapshot changes the fingerprint, and a window of another build refuses the memory instead of misreading it. It shows no levels rather than wrong ones. Raising `kTelemetryVersion` is still right when the meaning changes but the layout does not.
- **One memfd per open session**, 8768 bytes (three pages), whether or not a window is on the daemon. It is created when the session opens and freed when it closes. Version 2 added the analysed block's spectrum before it, its band values and the channel both spectra are of: 9408 bytes, still three pages, and a window of version 1 refuses it. Version 3 doubled the band values to 128, for the 64-band listening block's gain and level in each band: 9664 bytes, still three pages.
- **Nothing on the real-time side changes**: the engine thread publishes into the same seqlock, which now lives in a mapping.

## Alternatives considered

- **Snapshots as D-Bus signals.** Sixty messages a second per session, broadcast to every window whether it shows the session or not. The facade was built to avoid exactly this (ADR-0003).
- **A named POSIX shared-memory object** (`shm_open`). It needs a name nobody else can take or guess, and cleanup after a crash. A memfd has no name in any namespace and goes when its last mapping does.
- **Passing the descriptor at activation, or in a signal.** Activation hands the daemon nothing from the window. A signal would give every client every session's memory. A method answers only the caller.
- **A socket streaming snapshots.** A copy and a wakeup per frame per window, for data a mapping gives for nothing.
- **Sharing the writable memfd** (a `dup()`). One bug in a window could then write the engine's telemetry. The read-only reopen costs one `open()`.

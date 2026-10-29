<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0003: In-process engine first, behind a D-Bus-shaped facade

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

In the long run the engine has to run in the background as a service. Sessions should keep running and recording when the window closes, and a command-line tool and a scheduler need an engine to talk to. Building that inter-process plumbing first, though, would delay the core features: capture, routing, meters and the main blocks.

## Decision

- **At first the engine is a library inside the GUI process.** It runs on its own thread, behind a **facade** in `engine/core`.
- **The facade is designed as if it were already remote:**
  - Its D-Bus introspection XML, interface `io.github.reverblabr.Engine` (MIT licence), is written first and is the source of truth.
  - Only D-Bus-compatible types are used; structured data travels as JSON strings.
  - All calls are asynchronous. No call blocks the GUI thread for more than 1 ms.
  - Model changes arrive as a snapshot followed by numbered patches, and the client resynchronises when it sees a gap.
  - Errors are `{code, args}` and never text.
  - Meter data is read through a snapshot reader, laid out so it can move into shared memory.
- **A mock engine implements the same facade**, so interface work can proceed independently.
- **The engine later moves into a separate service, `reverblabrd`.**

## Consequences

- Early work moves faster: there is no process management, service activation or IPC debugging yet.
- Moving the engine into a daemon is mechanical. The interface already exists, only its transport changes, and the checks against the XML carry over.
- The GUI can't reach past the facade. `reverblabr-engine` links the lower engine layers privately, so their headers aren't even visible to the GUI.
- Until the engine has its own service, closing the window stops all running sessions.

## Alternatives considered

- **The daemon from the start.** Cleaner end state, but weeks of plumbing before the first audible result, and every early interface mistake would be baked into a public IPC API.
- **The GUI calling engine internals directly.** Fastest at first, but the later split would become a rewrite, and the GUI would inevitably come to depend on engine threading details.

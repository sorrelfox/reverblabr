<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0006: Application ID `io.github.reverblabr.Reverblabr` and naming scheme

- **Status:** Accepted; the engine's interface name amended by [ADR-0020](0020-public-dbus-api.md) (`io.github.reverblabr.Engine1`, one object)
- **Date:** 2026-09-19

## Context

Identifiers end up in places that are hard to change once users depend on them:

- desktop file names and the Wayland app ID
- D-Bus names that other programs call
- configuration and data paths on users' machines
- PipeWire node names that users' tools and WirePlumber rules match against

They should be fixed before anything is released or stored.

## Decision

The reverse-DNS prefix is **`io.github.reverblabr`**.

| What | Name |
|---|---|
| Application ID: desktop file, Wayland app ID, AppStream ID | `io.github.reverblabr.Reverblabr` |
| Engine D-Bus service and interface | `io.github.reverblabr.Engine` |
| D-Bus object paths | `/io/github/reverblabr/Engine`, `/io/github/reverblabr/Sessions/<uuid>` |
| QML module | `io.github.reverblabr` |
| PipeWire node names | `reverblabr.<session-uuid>.seg<N>`, with `application.name = Reverblabr` |
| Executables | `reverblabr` (GUI), later `reverblabrd` (engine service) and `reverblabr-ctl` (CLI) |
| Data and configuration directories | `reverblabr` under the XDG base directories |
| Translation domain | `reverblabr` |

The full naming table, which also covers ports, block types, error codes, logging categories and namespaces, is in [the architecture document](../architecture.md#naming).

## Consequences

- These names are part of users' data and integrations. Changing one later needs a migration, such as moving settings or keeping an old D-Bus name alive, and a new ADR.
- The `io.github.` prefix follows the common convention for projects without their own domain. If the app is ever published on Flathub, the `io.github.reverblabr` namespace has to be verifiable, through a code-hosting account or organisation called `reverblabr`, or the ID changes as part of that decision.
- Tools and rules can match everything of ours by the `reverblabr` prefix, for example `pw-dump` filters or WirePlumber rules that hide our nodes from volume applets.

## Alternatives considered

- **A domain of our own** (for example `org.reverblabr`). There is no such domain today.
- **Using `reverblabr` without a reverse-DNS prefix everywhere.** It doesn't follow the freedesktop conventions for application and D-Bus IDs, and it risks collisions.

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0002: Native libpipewire, WirePlumber as the policy peer, fan-out links only

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

Reverblabr captures audio from applications, input devices and the whole system, processes it, and plays it to devices. On current Linux desktops all audio flows through **PipeWire**. **WirePlumber**, the session manager, decides the policy: default devices, where ordinary streams play, volumes, and Bluetooth profiles.

In PipeWire every application stream is a node with ports, and any client may link ports. An output port can feed several input ports (fan-out), and an input port sums everything linked into it.

## Decision

- **Use libpipewire directly**: the registry, filter nodes (`pw_filter`), the link factory and the `default` metadata. There is no libwireplumber dependency and no GLib main loop.
- **WirePlumber stays in charge of policy:**
  - We read default devices from the `default` metadata.
  - We never override WirePlumber's decisions.
  - The app needs no WirePlumber scripts or configuration to work. Optional configuration snippets may be offered later for diagnostics. *Amended by [ADR-0013](0013-virtual-devices.md): on the user's request the app writes PipeWire drop-ins that create virtual devices; it still writes no WirePlumber configuration.*
- **Only add links.** Capturing an application adds links from its stream to our node. We never move, unlink or reconfigure another application's stream, so it keeps playing where it was.
- **We create every link ourselves**, with `object.linger = false`, so the links disappear when our client disconnects, including after a crash.
- **Our nodes don't auto-connect** (`node.autoconnect = false`). They carry descriptive names: `reverblabr.<session-uuid>.seg<N>`, and `Reverblabr — <session name>` as the description.
- **One filter node per running session.** Out-of-process plugins will later split a session into several nodes that PipeWire schedules within the same cycle.
- **Channel mapping is explicit.** Only ports with the same channel position are linked, and all channel mixing happens in our DSP.

## Consequences

- No drivers, no root access and no special permissions. Native packages can capture any application. *(Amended by [ADR-0013](0013-virtual-devices.md): virtual devices for all users go through a root prompt.)*
- The application being captured is undisturbed: it keeps its device, volume and routing.
- We have to follow the graph as it changes:
  - streams come and go, and ports appear after their nodes
  - applications restart, and WirePlumber moves streams between devices
  - this needs a registry mirror, and a link manager that links when ports appear
- An input port sums its links at unity gain, so links can't be used to downmix. That is why the channel mapping is explicit and mixing happens in our DSP.
- Some of this behaviour can only be settled by experiment: link timing, stream restarts, channel remixing, alert streams, and node class side effects such as Bluetooth profile switching. Spike S1 measures it and records the results in ADR-0007.

## Alternatives considered

- **libwireplumber.** A higher-level object manager, but it brings another dependency and a GLib loop, and it blurs the line between our code and the policy daemon.
- **The PulseAudio API (through pipewire-pulse).** No port-level control, and system capture only through device monitors, which can't exclude single applications.
- **The JACK API (through pipewire-jack).** Port-level linking, but no access to PipeWire metadata or stream properties, and JACK's fixed-period assumptions.

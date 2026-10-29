<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0007: Capture and linking semantics

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

[ADR-0002](0002-native-pipewire-and-wireplumber-as-peer.md) decided that we capture by adding our own links to other nodes' ports. Spike S1 measured how that behaves on PipeWire 1.6.8, WirePlumber 0.5.17 and Plasma 6.7. The engine's registry mirror, link manager, channel mapping, feedback guard, session node and inventory, and the source and output blocks, are built on these facts.

The evidence comes from two places:

- [`tests/pw/test_capture_semantics.cpp`](../../tests/pw/test_capture_semantics.cpp), one integration test per fact, run in the private PipeWire instance (`ctest -R pw.capture-semantics`)
- a throwaway prototype of per-app capture with its own tests, which was deleted once the link manager had landed and taken over what it proved

The measurements, commands and `pw-dump` excerpts are in [the spike notes](../dev/spike-s1-capture.md). In short:

| # | Question | Finding |
|---|---|---|
| 1 | Fan-out | Linking a stream into our node leaves it playing to its sink, at the same level. |
| 2 | When to link | A stream's node always appears before its ports. Moving a stream to a sink with another layout destroys its ports and creates new ones; **port ids come back with other channels**. A restarted app is a new node, which may reuse the old node id. |
| 3 | Volumes | A stream's volume and mute are applied **before** its output ports. A sink's volume and mute are **not** applied to its monitor ports (`monitor.channel-volumes` is false by default, as on ALSA sinks). |
| 4 | Layouts | A stream takes the layout of its sink. A stereo app on a 5.1 sink has six ports, and only FL and FR carry sound. On a mono sink it has one `MONO` port with the downmix. On an AUX sink it stays FL/FR. |
| 5 | Summing | Links into one input port are summed at unity gain: FL and FR of the same signal into one port gives +6.02 dB. |
| 6 | Alerts | A KDE notification sound (KNotification → libcanberra → pipewire-pulse) is a `Stream/Output/Audio` node with `media.role = "Notification"` and an `event.id`. |
| 7 | Re-plays | Loopbacks, filter chains, combined sinks and echo cancellers re-play audio as `Stream/Output/Audio` nodes that carry `node.link-group`. An application's own stream has none. |
| 8 | Feedback | PipeWire neither refuses nor breaks a loop: a node that captures a sink's monitor and plays back into it keeps both links, with no error. What the circulating sound then does depends on the machine: on the development machine it rang on at full level after the source stopped, in a CI container it decayed to silence. |
| 9 | Node identity | PulseAudio clients (Plasma's volume applet and microphone indicator, pavucontrol) see **no `pw_filter` node** of any class, even while it records; they do see a capture `pw_stream`. The policy never links a node with `node.autoconnect = false`, whatever its class. WirePlumber switches a Bluetooth headset to its headset profile only when a `Stream/Input/Audio` node without `node.link-group` is linked to the headset's source (from `autoswitch-bluetooth-profile.lua`; not tried on hardware). |
| 10 | Clean-up | Links created with `object.linger = false` disappear when their creator is killed with SIGKILL; the captured app keeps playing. Links made by `pw-link` linger by default. |

## Decision

**Identity.** Nodes and ports are identified over time by `object.serial`, never by id alone. A cached port whose serial changed is a different port.

**Link timing.** Links are driven by **port** appearance. The link manager keeps a desired state (which source ports go to which of our ports) and reconciles it on every port and node event. Node appearance only records that a stream matches. Restarts, moves and format changes all come down to "new ports appeared" and need no special case.

**Links.** Every link we create has `object.linger = false`. We never create lingering links, and never remove or move a link we didn't create.

**Channel mapping.**
- A link joins two ports with the same `audio.channel`, or a source port and a lane that the channel map explicitly assigns to it. We never link two different source channels into one of our ports; mixing and downmixing happen in our DSP.
- Several **streams** may be linked into the same lane, because summing whole streams is exactly what a sink does. System Audio relies on this.
- A captured stream is taken in whatever layout it has at the moment. The source block gets one lane per port (FL, FR, FC, LFE… or MONO), and the DSP converts it to the block's layout. Dropping FC or the surrounds of a real 5.1 stream would lose sound.

**Volumes.** Captured streams already carry their user volume and mute; we don't apply them again. The same holds for a sink's monitor, which is taken before the sink's volume. Our Output Device links go through the sink, so the user's device volume applies to our output as usual.

**System Audio strategy: per-stream merge.** The System Audio block links every `Stream/Output/Audio` node except:
- our own nodes
- streams with `node.link-group` (re-plays), so that audio sent through a loopback, filter chain, combined sink or echo canceller is counted once, at its source
- alert streams, unless the block includes them
- the applications the user excluded

Sink monitors are not used. They can't exclude an application, and they contain our own output whenever we play to the same sink.

**Alert streams.** A stream is an alert when `media.role` is `Notification` (case-insensitive; also `event`, the PulseAudio name), or when it has an `event.id`.

**Node class.**
- The session's processing node sets **no `media.class`** and `node.autoconnect = false`. It is neither a device nor an application stream, and the policy leaves it alone.
- We don't try to appear in the Plasma microphone indicator with the processing node; it can't. Showing that we record is Reverblabr's own job: a tray indicator while capture runs.
- An Input Device block reading a **Bluetooth** source needs a node of class `Stream/Input/Audio` linked to that source. Without it the headset stays in its playback profile and its microphone delivers silence. How that node hands its audio to the session node without adding latency is a question for the filter-runtime spike S2. The same node also makes microphone use visible to Plasma if it is a `pw_stream`. The Input Device block implements it; it must be tried with a real headset.

**Feedback.** The feedback guard refuses any link that would close a loop through our nodes. When walking the graph it treats a sink's input and its monitor output as connected, because audio played into a sink comes out of its monitor.

## Consequences

- The link manager is a reconciler over serial-keyed state rather than a set of event handlers. The prototype shows this is enough: it re-captured an app across 20 restarts and followed it across stereo, 5.1 and mono sinks without special cases.
- Source blocks need dynamic input lanes, because a stream's layout depends on the sink it plays to and can change while it plays. This feeds into the live-update design.
- The channel-mapping tables must cover every layout a sink can impose, including MONO, and stay the only place where channels are mixed.
- The inventory has to classify streams: application, re-play (link group), alert, or our own. The System Audio exclusion list and the Application picker read that classification.
- Bluetooth microphones need a separate capture node, and that design is open until S2 and the Input Device block settle it.
- The microphone indicator in Plasma won't list Reverblabr while it captures applications. Our own tray indicator has to make capture visible.
- Every fact above has a test. A PipeWire or WirePlumber update that changes one fails `pw.capture-semantics`, and names the fact.

## Alternatives considered

- **Link when the node appears.** Its ports don't exist yet, and they change when the stream moves. It would need a second, port-driven path anyway.
- **Capture System Audio from the default sink's monitor.** One link and exactly what the user hears, but it can't exclude applications, it captures our own output when we play to that sink (feedback), and it misses streams playing to other sinks.
- **Let links downmix by linking several channels into one port.** Links sum at unity gain, so this adds 6 dB for two correlated channels and has no coefficients to tune.
- **Give the session node `media.class = Stream/Input/Audio`, so that Bluetooth switching and the microphone indicator notice it.** The indicator doesn't see filter nodes of any class, so it doesn't help there. And a node that also plays audio shouldn't present itself to the policy as a capture stream; a dedicated node for Bluetooth sources says what it is.

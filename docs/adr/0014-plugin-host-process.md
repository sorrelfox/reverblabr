<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0014: Plugins in host processes of their own, as PipeWire nodes the session is split around

- **Status:** Accepted
- **Date:** 2026-09-27

## Context

Third-party plugins (LV2, CLAP and LADSPA; VST3 later) are code we did not write, and some of them crash. A plugin that crashes inside the engine's process takes every session down with it. [The process model](../architecture.md#process-model) has said from the start that each plugin runs in a `reverblabr-plugin-host` process of its own, as a PipeWire node of its own, and that a crash takes down only that host.

That leaves three questions:
- how the session's graph and the host's node fit together;
- what they say to each other, and how;
- how the engine knows a host has gone or stopped.

The first matters most. A session today is one PipeWire filter node (the session node, ADR-0008). All of its blocks run inside that node's cycle, in the order the graph compiler gave them. A plugin in another process can't run inside that cycle. So either the session is **split into segments**, one node for each stretch between plugins, linked through the host nodes, or the host is **called from inside the session node** and waited for.

The first choice is only free if PipeWire runs segment, host and segment in **the same cycle**. That had to be confirmed first. The spike ([notes](../dev/spike-plugin-host.md)) measured it on PipeWire 1.6.9 with the private test instance. It used a node in one process feeding a node in another process, which fed a third node back in the first:

| # | Question | Finding |
|---|---|---|
| 1 | Same cycle? | Yes, at quanta 64, 256 and 1024: every cycle the last node heard what the first played in **that** cycle (1500/1500, 375/375, 94/94). A process boundary adds no quantum. |
| 2 | A host that stops (SIGSTOP) | **Everything after it stops too**: the last node ran 0 cycles in a second while the first ran 750. PipeWire waits for the host. |
| 3 | Linking around a stopped host | The last node hears the first again **9.9 ms** after the links change, including starting three `pw-link` processes. |
| 4 | A host that dies (SIGKILL) | Its node goes. The nodes around it keep running and the last hears silence. |

Finding 2 makes a hang worse than a crash. A crash silences one path, but a hang stops everything downstream of the host, **session output included**. The engine has to notice a hang as surely as a crash.

## Decision

**A plugin block splits the session.** The graph compiler cuts the session at each plugin block. Each segment between plugins is a filter node of ours, and each plugin is a host's node. The engine links them in order, and PipeWire runs the whole chain in one cycle (finding 1). There is no extra latency beyond what the plugin itself reports.

How the session is cut:
- **A block's segment is the number of plugins on the path to it that has the most.** Every wire then runs forward, from a segment to the same one or a later one, and a session with no plugin in it is one segment, exactly as before.
- **A plugin's inputs are gathered in one segment first**, the one its latest input comes from, and leave for the host together. They never go straight from one host to another. So two paths that meet at a plugin, one of them through another plugin, meet in a graph first, where latency compensation can hold one back. If they met in PipeWire instead, they would be summed as they come.
- **Whatever crosses goes through boundary blocks**, which the session node gives ports like any source or output.
- **An arriving boundary carries the latency the sound has picked up before it.** So the compensation inside a later segment's graph lines up a path through a plugin with a path around it.

**One host process per plugin instance.** `reverblabr-plugin-host --format <f> --plugin <id> --instance <id> --control-fd 3 --shm-fd 4 [--latency N]` makes one `pw_filter` named `reverblabr.plugin.<instance>`. Its ports are named from the plugin's port labels. It has no Qt, so a host starts with the plugin, PipeWire, the protocol and nothing else (enforced by `rb_check_layering`). A crashing plugin takes only its own instance with it.

**Two channels, chosen by what each carries:**

| Channel | Carries | Why |
|---|---|---|
| Shared memory (a memfd, `plugin::SharedBlock`) | Parameter changes, engine → host, in an SPSC ring of 1024. The host's heartbeat (cycles run), its plugin's latency, and the rate it is prepared for or wants, host → engine. | Read and written on the host's audio thread, so it must be lock-free plain values. A knob is heard from the next cycle's first sample, with nothing allocated or sent through a socket. |
| A `SOCK_SEQPACKET` socketpair | Ready (node name and latency), Latency, State, Error, GetState, SetState, Quit | Anything that owns memory. Each message arrives whole, and the host's main thread handles it. |

The memory starts with a magic number and a layout version. A host that finds anything else refuses to run instead of misreading it.

**The host's audio cycle is `plugin::HostProcessor`**, kept apart from its PipeWire node for the same reason `SessionProcessor` is. It beats the heart first, then drains the ring and runs the plugin. The cycle can be run, and checked for allocation, without a server. A cycle at a rate the plugin wasn't prepared for comes out silent and asks for that rate, and the host's main thread prepares a fresh instance with the same state.

**The engine watches each host in three ways** (`pw::PluginHostProcess`):
- **exit**, through a pidfd on the engine's event loop. It arrives as an event, with no polling (measured at 0.5–6 ms);
- **a hang**, by the heartbeat standing still while the node feeding the host keeps running: for three of its cycles **and** at least 40 ms, by default (measured at 45 ms at quantum 256). The cycle count means a graph that is simply not running is never taken for a hung host. The time floor means a host that a busy machine ran late for a few cycles at a small quantum (three at quantum 64 is 4 ms) isn't either;
- **the engine's own death**, through `PR_SET_PDEATHSIG`: a host never outlives the engine that started it, so no orphaned node is left in the graph.

**The bypass is a relink** around the host (finding 3). A hung host is then killed. It is never waited for. As built:
- the links go from the segment before the host straight to the one after it, reconciled at once;
- a host that had run for ten seconds is restarted by itself, and one that fails sooner waits for the person;
- a restarted plugin gets its state, then every value set since, which the host holds back until the state is in (the StateLoaded message).
- **a heartbeat still for a second is a hang on its own**, whatever the segment before the host did. A host stopped while PipeWire was changing the graph (a quantum coming in) held the whole graph still, the segment feeding it included, so the cycle count never moved. Stillness only counts while the watch itself keeps running: a watch held up for more than a tenth of a second starts counting again, so a machine waking from sleep isn't taken for a hang.

**Latency goes where every block's goes.** The host reports its plugin's latency in the Ready message and in the shared block. The session's plugin block carries it as `BlockInfo::latencySamples`, so latency compensation treats a plugin like any other block.

## Consequences

- A session with plugins is several nodes, not one. Those nodes are linked **to each other**, which the link manager has so far refused to do between our own nodes. Plugin support changes that, along with the graph compiler's split and the runtime keeping the segments in step.
- A hung host stops the sound after it until the watchdog fires, which takes 40 ms or three cycles, whichever is longer, plus a relink. That is short enough to hear as a dropout rather than a stop. Being in-process wouldn't be better: a hung plugin there hangs the whole session.
- Parameters go straight to the host's audio thread through the ring, never through the engine's own. A parameter's value lives in the engine's model, and the host only applies it.
- A plugin's state is read on the host's main thread while its audio thread may be applying a change. That's how LV2 and CLAP ask for state too, and why a plugin keeps whatever the two threads share safe itself. The built-in test plugin keeps its gain in an atomic.
- Each plugin instance costs a process: a few megabytes and a PipeWire client. Grouping several plugins into one host is possible later without changing the protocol. They would share a fate, so it would be opt-in.
- Until the formats arrive, the host runs built-in test plugins (`--format builtin`, `test.gain`). The whole path, from process to protocol, node and watchdog, is tested and measured with them (`pw.plugin-host`).

## Alternatives considered

- **Calling the host from inside the session node** (the session node writes a cycle into shared memory, wakes the host and waits). This keeps one node, but the session's audio thread would block on another process every cycle, which the [real-time rules](../architecture.md#real-time-rules) forbid. A host that hangs would hang the session node itself, not just the path after it. PipeWire already schedules separate processes in one cycle (finding 1), with its own eventfd wake-ups and a driver that knows about them.
- **Plugins in the engine's process.** No crash isolation, which is what the host process is for.
- **One host process for all plugins.** Cheaper, but one bad plugin takes all the others with it.
- **Parameters over the socket.** The host's main thread would have to pass them to its audio thread, adding a hop and a queue. A ring the audio thread reads directly is simpler and hears a change a cycle sooner.
- **A clock-based watchdog alone** (no heartbeat for N milliseconds). It can't tell a hung host from a graph that isn't running, such as a suspended driver or a session that was stopped. Comparing against the upstream node's cycles can. The clock stays, but only as a floor.
- **A cycle-count watchdog alone.** At quantum 64, three cycles is 4 ms, which a loaded machine can take from a healthy host. Bypassing a plugin for that would be worse than the dropout it causes.

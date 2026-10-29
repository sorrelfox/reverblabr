<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0008: Filter-node runtime and graph handover

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

Each running session is one `pw_filter` node ([ADR-0002](0002-native-pipewire-and-wireplumber-as-peer.md)). Its `process()` runs on PipeWire's real-time data thread, and runs the session's compiled graph. The graph is replaced while the node runs, whenever the user edits the session. Before building the session node and live graph updates, spike S2 measured how such a node behaves on PipeWire 1.6.8 and WirePlumber 0.5.17.

The evidence came from a throwaway prototype and its tests, run in the private PipeWire instance; it was deleted once the session node and live graph updates had landed and taken over what it proved. The numbers are in [the spike notes](../dev/spike-s2-runtime.md). In short:

| # | Question | Finding |
|---|---|---|
| 1 | Scheduling when unlinked | With `node.always-process = true` the node runs with no links at all, driven by the `Dummy-Driver` at the default quantum. The idle sink stays suspended, so no device is kept awake. Linked to a sink, the sink drives it; unlinked again, the dummy driver takes over and the sink suspends. `process()` never stood still for longer than about one cycle (21 ms at quantum 1024) through all of this. Without `always-process` an unlinked node never runs. |
| 2 | Quantum and rate | The quantum followed `clock.force-quantum` through every value from 32 to 8192 frames. Ten switches between 64 and 2048 left a sine clean (residual −141 dBFS, float rounding). A forced rate (44.1, 96 and back to 48 kHz) shows in `position->clock.rate`; `process()` saw every change. A switch of either kind can click while the graph is reconfigured (−8 to −27 dBFS in some runs); nothing lasts. |
| 3 | NULL buffers | `pw_filter_get_dsp_buffer()` returns NULL for every **unlinked** port, inputs and outputs alike, and never for a linked one. |
| 4 | Dynamic ports | 1,000 add/remove cycles while running were safe under ASan and click-free, following one order: add the port, publish a graph that uses it; publish a graph without it, wait until that graph is live, then remove the port. |
| 5 | Graph handover | Both handovers passed 10,000 swaps at one swap per cycle with a sine playing, click-free (residual −141 dBFS) and clean under ASan and TSan. An **atomic slot with an epoch** goes live only when a cycle runs (1.33 ms at quantum 64); a **blocking `pw_loop_invoke()`** on the data loop swaps between two cycles and returns once it has run, in about 10 µs, and never returned early. Published back to back, 10,000 swaps take 40 ms (atomic) or 74 ms (invoke), and occasionally cost the session a cycle, which clicks. While the node is **not scheduled**, the atomic handover never completes and its graphs pile up; the invoke still completes, in 26 µs. |
| 6 | Data thread | PipeWire leaves FTZ and DAZ **off**. Set at the start of `process()`, they stayed set in every later cycle. A sine session took 6–14 µs per 256-frame cycle (0.1–0.3 % of the cycle), measured with `clock_gettime` inside `process()`. After a `process()` call three cycles long, `SPA_IO_CLOCK_FLAG_XRUN_RECOVER` was **never set**, with the dummy driver or a sink driving; the graph position jumped instead. |
| 7 | Qt integration | A `pw_loop` driven by a `QSocketNotifier` on its fd delivers every main-loop event on the Qt thread, with no lock. `process()` keeps running on PipeWire's data thread either way. With `pw_thread_loop`, events arrive on the loop's own thread, and every call needs the lock. |
| — | Chained node (from S1) | A pass-through `Stream/Input/Audio` filter placed in front of another node adds no latency: a ramp arrived sample-identical through it and directly, in the same cycle. |

## Decision

**Scheduling.** The session node sets `node.always-process = true`, so meters and generators keep running with nothing linked. Stopping a session destroys its node; there is no "paused" node.

**Buffers.** All per-cycle buffers are sized for 8192 frames (`clock.quantum-limit`) when the graph is compiled. A quantum change needs no preparation. `process()` handles any `n_samples` from 1 to 8192.

**Rate.** A rate change is the one change that needs preparation. `process()` compares `position->clock.rate` with the rate the graph was prepared for. On a change it outputs silence and asks the control thread for a graph prepared at the new rate (coefficients, delay lengths). It never re-prepares on the data thread.

**NULL buffers.** A NULL input buffer is silence. A NULL output buffer is skipped. Every non-NULL output buffer is written in every cycle, with silence if the graph doesn't use it.

**Graph handover: blocking invoke on the data loop.**
- The control thread swaps the live graph with `pw_loop_invoke(pw_filter_get_data_loop(), …, block = true)`. The callback, which runs on the data thread between two cycles, only exchanges pointers.
- When the call returns, the old graph is out of use, and the control thread frees it directly. No epochs, no retire queue for graphs, no pile-up while the node isn't scheduled.
- The callback runs on the real-time thread, so it follows the real-time rules: no allocation, no lock, no system call.
- The engine thread may block for up to one cycle on a swap. That is acceptable there; the GUI thread never calls it.

**Swap rate.** The engine publishes at most one graph per cycle, and coalesces edits that arrive faster (a parameter drag changes parameters, not the graph). Waking the data loop every few microseconds can cost it a cycle.

**Ports.** Ports are added and removed only on the main loop, around a handover: add the port, then publish a graph that uses it; publish a graph that no longer uses it, then remove the port once the call has returned.

**Denormals.** `process()` sets FTZ and DAZ at its start, if they aren't set yet. It is one register read in the common case.

**Load and xruns.**
- `process()` measures its own time with `clock_gettime(CLOCK_MONOTONIC)` (vDSO, real-time safe) and reports the peak load per telemetry period.
- It counts an xrun whenever the graph's own cycle counter (`clock.cycle`) has moved on by more than one step, whenever a cycle doesn't start where the previous one ended (`clock.position`), whenever the driver's accumulated estimate (`clock.xrun`) goes up, and when `SPA_IO_CLOCK_FLAG_XRUN_RECOVER` is set, in case an ALSA driver sets it. The flag alone is not enough: it never came in the spike.
- **Only `clock.id` says the node changed driver, and only a change of driver may suppress any of the rest.** The position, the cycle counter and the xrun estimate all belong to the driver, so none of them can be compared across a handover, and nothing else in the cycle means a handover. SPA documents `clock.cycle` as counting how often the graph has been *started*; this server counts its *cycles* with it, so it goes up by one every cycle, its step is how many cycles the graph ran while this node ran one, and a step backwards is the graph being restarted. Testing it for a change rather than for its step made every cycle look like a change of driver, suppressed every other sign along with it, and left the count sitting at zero through anything.

**Event loop.** The engine thread (a `QThread`, [ADR-0003](0003-in-process-engine-behind-facade.md)) runs PipeWire's main loop through a `QSocketNotifier` on `pw_loop_get_fd()`. There is no `pw_thread_loop` and no lock. Registry events and replies arrive on the engine thread, where the engine's state lives.

**Bluetooth microphones (from ADR-0007).** The separate capture node is a pass-through `pw_filter` of class `Stream/Input/Audio`, linked in front of the session node. It adds no latency. Like the session node, it is invisible to PulseAudio clients.

## Consequences

- The compiled graph owns every buffer it uses, sized for 8192 frames. Block instances live outside it, so their state (filter memories, oscillator phase) survives a swap. The spike's oscillator did exactly that across 10,000 swaps.
- Live graph updates are a control-thread protocol: compile, invoke, free. Crossfades for changed paths and bypass are features of the graph, not of the handover.
- The xrun counter and load meter come from our own measurements, and are reported through telemetry. Because the counter is what everything downstream believes, it is proved by injecting the defect it exists for rather than by watching a session that is well: `pw.session-node` holds a block in the graph past its deadline in one cycle of eight and requires the figure to move. A counter stuck at zero passes every test that only ever looks at a healthy run, which is how one stayed stuck.
- The rate-change path needs an offline test: process at 48 kHz, switch to 44.1 kHz, check silence and then correct output.
- Every finding has a test, now in `pw.session-node` rather than in the prototype: always-process, NULL buffers, dynamic ports, the handover, and changes of quantum and of rate are all checked against a real server. The one finding that is no longer watched for its own sake is that PipeWire never sets the xrun flag, and it no longer matters: the counter treats the flag as one sign among several, so a PipeWire that started setting it would simply be believed.

## Alternatives considered

- **Atomic slot with an epoch.** Wait-free for the control thread, but a graph goes live only when a cycle runs, so every swap waits up to a cycle and the engine can't tell a slow handover from a stalled one. A node that isn't scheduled never takes the graph at all, so retired graphs pile up and the publisher can't know when to free them.
- **`pw_thread_loop`.** The standard pattern, but every call needs the lock, and every event arrives on a thread the engine doesn't own, to be marshalled over. A `QSocketNotifier` gives both for free.
- **Re-preparing on every quantum change.** Unnecessary: buffers sized for the quantum limit cover every quantum PipeWire uses.
- **Relying on `SPA_IO_CLOCK_FLAG_XRUN_RECOVER`.** It never appeared, so xruns would go unnoticed.

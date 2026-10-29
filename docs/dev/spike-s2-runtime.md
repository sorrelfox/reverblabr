<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Spike S2: filter-node runtime, the evidence

Measurements behind [ADR-0008](../adr/0008-filter-runtime.md). Everything ran in the private PipeWire instance of the test harness ([`tests/pw/`](../../tests/pw/README.md)).

- **Versions:** PipeWire 1.6.8, WirePlumber 0.5.17, Fedora 44, AMD Ryzen 9 (debug build unless noted)
- **Prototype:** deleted once the session node and live graph updates had landed; `pw.session-node` checks the same behaviour through the engine's own node
  - `SpikeNode` wraps one `pw_filter` and records what its data thread sees in every cycle.
  - `Session` plays a 997 Hz sine at −12 dBFS through a replaceable graph. The graph owns heap buffers and names the ports it writes, so ASan catches a graph or port freed too early.
- **Click check:** `CaptureNode::measureSine()` in the harness computes x[n] − 2cos(ω)·x[n−1] + x[n−2] on every captured sample. It is zero for a pure sine, so a click, a gap or a repeated block shows up as a large residual. The residual of a clean float sine is about −141 dBFS; the acceptance limit was −60 dB.
- **Lost cycles:** a cycle the graph loses clicks by itself, whatever is being tested, and so does a cycle the session misses while the capture node runs it, because the capture then reads the previous block again. Instrumented builds do both now and then (under AddressSanitizer, more than half the windows of a long run). The tests therefore measure in windows and judge only the windows in which neither node lost a cycle (`clock.position` stayed continuous) and both ran the same number of cycles. The number of windows thrown away for this is printed.
- **A control window:** even that is not enough on a busy machine, where a cycle can be late without being lost. Before swapping anything, the swap test measures a window with nothing happening. If that control is clean, the windows measured while swapping must be clean too; if the machine can't hold a clean sine even at rest (a loaded CI runner often can't), the click check is reported instead of required, and the rest of the test still stands. After a quantum or rate change, the sine has to come back to the quality the control showed, not to an absolute figure.

To reproduce:

```sh
ctest --preset debug -R pw.session-node --output-on-failure
ctest --preset asan -R 'spike.s2'                         # the same under AddressSanitizer
```

## 1. Scheduling when unlinked

```
[measure] unlinked, always-process: 25 cycles in 0.5 s, driver Dummy-Driver, quantum 1024
[measure] unlinked, no always-process: 0 cycles
[measure] default sink state: suspended
[measure] driver unlinked: Dummy-Driver; linked to the sink: rbtest.sink.stereo (sink running);
          unlinked again after 6.5 s: Dummy-Driver (sink suspended)
[measure] longest stop of process(): 20 ms
```

`position->clock.id` names the driver's node. An unlinked node with `node.always-process` is driven by the `Dummy-Driver`, a timer in the server, so no device is woken for it. The longest stop is sampled every 5 ms, and one cycle at quantum 1024 lasts 21 ms, so `process()` never missed a cycle through linking, unlinking and the sink's suspend.

The harness has no hardware sinks. With an ALSA sink the same rule applies (an idle, unlinked node is not linked to it, so it can suspend), but that is inferred, not measured.

## 2. Quantum and rate

```
[measure] forced quanta reached: 32 64 128 256 512 1024 2048 4096 8192
[measure] 10 switches 64 <-> 2048: quantum 64..2048, xruns 0, position jumps 10
[measure] worst residual in a switching window: -17.5881 / -17.5881 dBFS   (most runs: -140.973)
[measure] settled at quantum 256 FL: peak -12 dBFS, residual -140.973 dBFS over 24064 samples, 0 NULL buffers
[measure] forced rates reached: 44100 96000 48000; rate changes seen by process(): 3
[measure] first 300 ms back at 48 kHz: residual -140.973 / -140.973 dBFS
[measure] settled at 48 kHz FL: peak -12 dBFS, residual -140.973 dBFS over 23552 samples, 0 NULL buffers
```

Set with `pw-metadata -n settings 0 clock.force-quantum N` and `clock.force-rate N`. The prototype's buffers are sized for 8192 frames once, and nothing is prepared again when the quantum changes.

A switch of either kind can click while PipeWire re-does the buffers: residuals of −8 to −27 dBFS turned up in about one run in four, in the window containing a switch. Each quantum switch also shows as a jump in the graph position, one per switch. Once settled, the sine is clean again in every run, which is what the tests require. The oscillator's step is fixed for 48 kHz, so at other rates the tone is simply off-pitch; the real engine re-prepares its graph on a rate change.

## 3. NULL buffers

```
[measure] unlinked: 30 NULL inputs and 30 NULL outputs in 15 cycles (2 ports each)
[measure] inputs linked: 0 NULL inputs in 15 cycles
```

Every unlinked port, input or output, returns NULL from `pw_filter_get_dsp_buffer()` in every cycle.

## 4 and 5. Ports and graph handover

The acceptance run: 10,000 graph swaps while the sine plays, **one swap per cycle** at quantum 64 (750 cycles a second), then 1,000 cycles of adding a port, publishing a graph that writes it, publishing one that doesn't, and removing the port.

```
[measure] windows of 1 s while swapping: 27, of which 0 had a lost or unmatched cycle
[measure] atomic+epoch: 10000 swaps live in 26613 ms, slowest publish-to-live 1588 us; 1000 port cycles; graphs still held 1; xruns 0, position jumps 0; invokes that returned before their swap ran 0
[measure] invoke: 10000 swaps live in 13333 ms, slowest publish-to-live 610 us; 1000 port cycles; graphs still held 1; xruns 0, position jumps 0; invokes that returned before their swap ran 0
[measure] during swaps FL: peak -12 dBFS, residual -140.973 dBFS over 672000 samples, 0 NULL buffers
[measure] after port cycles FL: peak -12 dBFS, residual -140.973 dBFS over 24000 samples, 0 NULL buffers
```

- **Atomic slot with an epoch.** The data thread takes the next graph at the start of a cycle and stores its epoch, so a swap goes live only when a cycle runs: 1.33 ms at quantum 64. With one swap per cycle on top, each round takes two cycles, which is why its run takes twice as long.
- **Blocking invoke.** `pw_loop_invoke()` on the data loop runs the swap between two cycles and returns once it has run. It was never seen to return early, and never failed.

Both runs are clean under ASan and TSan (`ctest --preset asan` and `--preset tsan`).

**Publishing faster than the graph runs** (10,000 swaps back to back, measured but not required):

```
[measure] atomic+epoch, 10000 swaps published back to back in 40504 us: position jumps 0, residual -140.973 dBFS
[measure] invoke, 10000 swaps published back to back in 73845 us: position jumps 0, residual -140.973 dBFS
```

So a swap costs about 4 µs to publish into the atomic slot and about 7–13 µs through an invoke. Most runs stayed clean, but in an earlier round of the same burst (about one run in six) the session lost a cycle and the sine showed a click of −7 to −29 dBFS: waking the data loop every few microseconds can cost it a cycle. Editing never produces that rate, and the engine paces swaps at one per cycle, so the acceptance run does too.

**When the node is not scheduled** (no `always-process`, no links):

```
[measure] not scheduled: atomic handover live 0, graphs held 1; invoke returned after 26 us, cycles run 0
```

The data loop services an invoke even when no cycle runs. The atomic slot is only emptied by `process()`. Five publishes left one graph held, because each publish takes the previous waiting graph back, but none went live.

## 6. The data thread

```
[measure] FTZ+DAZ already set by PipeWire: 0; cycles that lost our setting: 0
[measure] quantum 256, steady: longest process() 6.201 us, load 0.116269 % of the cycle; xrun flags 0, position jumps 0
[measure] one 16 ms stall, dummy driver (Dummy-Driver): load 300.082 %, xrun flags 0, position jumps 1
[measure] one 16 ms stall, sink driver (rbtest.sink.stereo): load 300.079 %, xrun flags 0, position jumps 1
```

- **FTZ and DAZ** (MXCSR bits 15 and 6) are off when the first cycle begins. Once `process()` sets them, no later cycle began without them.
- **Load:** the time is taken with `clock_gettime(CLOCK_MONOTONIC)` at entry and exit, and compared with the cycle's length (frames ÷ rate). Across runs the sine session took 6–14 µs per 256 frames.
- **Xruns:** a `process()` call busy-waited for three cycles.
  - `SPA_IO_CLOCK_FLAG_XRUN_RECOVER` was never set in the node's clock area, which does arrive through `io_changed(SPA_IO_Clock)`, and `clock.xrun` stayed 0. The server logged nothing at its default log level.
  - The graph position jumped once: the next cycle did not start where the stalled one ended, because the driver had moved on.
  - With ALSA drivers the flag may behave differently; the harness has none.

## 7. The Qt event loop

```
[measure] QSocketNotifier: replies on the Qt thread 1, round trip 0.0118505 ms; process() cycles 20
[measure] pw_thread_loop: replies on the Qt thread 0, round trip 0.102962 ms (with lock/unlock around each call)
```

A `pw_loop` is entered once (`pw_loop_enter`) on the Qt thread. A `QSocketNotifier` on `pw_loop_get_fd()` calls `pw_loop_iterate(loop, 0)` whenever the loop's epoll fd is readable. A hundred `pw_core_sync()` round trips were each answered on the Qt thread, and the filter's `process()` ran on PipeWire's data thread regardless. The thread-loop timing includes 50 µs polling for the reply, so it is not a fair speed comparison. The point is where the replies arrive, and that no lock is needed.

## Chained node (from S1)

```
[measure] ramp direct vs. through a Stream/Input/Audio node: largest difference 0 samples over 48128 samples
```

A ramp source feeds a comparing node twice: directly, and through a pass-through filter of class `Stream/Input/Audio`. The ramp's sample counter is exact in float. Both copies were identical sample for sample, so PipeWire ran the chain in dependency order within one cycle, and the extra node added no latency.

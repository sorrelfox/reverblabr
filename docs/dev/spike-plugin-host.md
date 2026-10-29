<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Spike: a node in another process, between two of ours

These are the measurements behind [ADR-0014](../adr/0014-plugin-host-process.md). Before splitting a session around plugin hosts, we check that PipeWire runs a chain that crosses processes in one cycle, and what the chain does when the process in the middle stops or dies.

- **Versions:** PipeWire 1.6.9, WirePlumber 0.5.17, Fedora 44.
- **Where:** the private instance of [`tests/pw/harness/run-private-pipewire.sh`](../../tests/pw/harness/run-private-pipewire.sh), driven by its dummy driver at 48 kHz, never a real session.
- **The probe:** throwaway, and not in the repository. What it established is now tested for real, against the real host, by `pw.plugin-host` (`tests/pw/test_pluginhost.cpp`). The section below describes it, and the figures that test prints are at the end.

## The probe

There are three `pw_filter` nodes, each with `node.always-process = true` and `node.autoconnect = false`:
- **A** plays and **B** listens. Both are in one process, on one `pw_thread_loop`.
- **P** is in a second process, forked and exec'd from the first. It copies its input to its output.

**A tells B which cycle it is.** Every sample A writes in a cycle is `(cycle mod 4096 + 1) / 8192`, where `cycle = clock.position / clock.duration` from the cycle's `spa_io_position`. B works out the same number for its own cycle and compares:
- **same** means B heard what A played in this cycle;
- **one late** means B heard the previous cycle's;
- **silent** means it heard nothing.

The chain is linked with `pw-link spike.a:out spike.p:in && pw-link spike.p:out spike.b:in`, and the quantum is forced with `pw-metadata -n settings 0 clock.force-quantum <q>`. The counts are taken over 2 s after a 1 s settle.

As a control, **A → B directly** (no P) gives the same result: every cycle is "same".

## 1. Same cycle across a process boundary

```
[measure] A -> host process -> B quantum 256: 375 cycles at B: same cycle 375, one late 0, other 0, silent 0
[measure] A -> host process -> B quantum 1024: 94 cycles at B: same cycle 94, one late 0, other 0, silent 0
[measure] A -> host process -> B quantum 64: 1500 cycles at B: same cycle 1500, one late 0, other 0, silent 0
```

PipeWire orders the three nodes by their links and wakes each in turn within the cycle, whichever process each is in. **A plugin host adds no quantum of latency.**

## 2. The process in the middle stops

The probe sends P `SIGSTOP`, then counts for 1 s:

```
[measure] host stopped (hung): A ran 750 cycles, B ran 0 in 1 s
```

B is scheduled after P, so PipeWire never gets to B and **B stops completely**. The rest of the graph goes on: A, and in a session everything not downstream of the host. For the engine, this means a hung plugin stops everything after it, including the session's output. It has to be noticed as surely as a crash.

## 3. Linking around it

With P still stopped, the probe removes P's links, links A → B, and times how long B takes to hear A again ("same" > 0):

```
[measure] relinked around the hung host: B hears A again after 9.9 ms (the pw-link calls included)
[measure] around the hung host: A ran 750, B ran 750 in 1 s, same 751
```

Most of the 9.9 ms is starting three `pw-link` processes. The engine will make its links through its own connection.

## 4. The process in the middle dies

The probe sends P `SIGKILL`:

```
[measure] host killed: A ran 750 cycles, B ran 750 in 1 s; B silent 750, same 0
```

P's node goes with its client, and its links go with the node. A and B keep running, and B hears silence until something links it again.

## The same, against the real host

`pw.plugin-host` repeats these with `reverblabr-plugin-host` running `test.gain`. It starts the host through `PluginHostProcess`, links it between two nodes of the test's own, and prints (debug build, one run):

```
[measure] quantum 64: 747 cycles heard after the host, 747 in the same cycle, 0 one late, 0 otherwise, 0 silent
[measure] quantum 256: 188 cycles heard after the host, 188 in the same cycle, 0 one late, 0 otherwise, 0 silent
[measure] quantum 1024: 47 cycles heard after the host, 47 in the same cycle, 0 one late, 0 otherwise, 0 silent
[measure] gain 0.5 heard after 4.0 ms, 1 host cycles
[measure] 256 samples of latency at quantum 256: 188 heard, 188 one cycle late, 0 same
[measure] killed host noticed after 0.39 ms; heard around it 9.4 ms after the kill (the pw-link calls included)
[measure] stopped host noticed after 45.00 ms
[measure] around the stopped host, heard again after 15.0 ms (the pw-link calls included)
```

- **A stopped host is noticed** after three cycles with no heartbeat while the node before it runs on, and at least 40 ms. The watchdog looks every 5 ms, so it saw it at 45 ms at quantum 256, where one cycle is 5.3 ms. Before the 40 ms floor was added, it saw it at 15 ms.
- **The latency line** shows that a plugin reporting 256 samples of latency delays by exactly that. At quantum 256, that is one cycle late every cycle.

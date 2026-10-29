<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0010: Our own real-time communication primitives, in a layer of their own

- **Status:** Accepted
- **Date:** 2026-09-20

## Context

The audio thread has to exchange data with the rest of the engine without ever waiting, allocating or taking a lock ([real-time rules](../architecture.md#real-time-rules)). Four kinds of exchange come up, and each of them appears more than once:

- parameter changes and commands going in, events coming out: a fixed-capacity queue between two threads;
- meter readings and gain reduction going out at the rate they are produced and being read at whatever rate the interface wants: a "latest value" carrier that neither side waits on;
- compiled graphs and buffers the audio thread has finished with: something that gets them to a thread that may call `delete`;
- the audio thread having something to say: a log it can write to without formatting or a system call.

It was still open whether the queue should be [moodycamel's `ReaderWriterQueue`](https://github.com/cameron314/readerwriterqueue) (BSD) or our own ring, and where the code should live. The layer table had no place for it: the pieces are used by blocks in `engine/dsp`, by the runtime in `engine/graph`, by the session node in `engine/pw` and by the facade in `engine/core`, so they cannot sit in any of them.

## Decision

**Write our own, in a new bottom layer `engine/rt` (`reverblabr-rt`), plain C++23 like `dsp` and `graph`.** Every other layer may use it; it uses nothing but the standard library. `engine/dsp` links it publicly, so it reaches everything above.

`RB_NONBLOCKING` moves here from `engine/dsp`, because it describes real-time code rather than DSP, and this is now the lowest layer that has it.

The layer holds five things:

| Type | For |
|---|---|
| `SpscQueue<T>` | a fixed-capacity queue between exactly two threads; `tryPush` fails rather than growing |
| `SeqlockValue<T>` | one small value published by a real-time writer and copied by readers, with no waiting either way |
| `TripleBuffer<T>` | the same job for a value too big to copy on every read: the writer fills a slot and publishes it with one exchange |
| `RetireQueue` | objects the audio thread has finished with, freed by whichever thread drains it |
| `RealtimeLogger` | a ring of fixed-size records a normal thread formats and writes |

### Why our own ring rather than moodycamel

- **A dependency has a price beyond the code**: a licence to track in REUSE and `CREDITS.md`, a package or a vendored copy for two distributions, and a version to follow. The queue itself is about sixty lines with a memory-ordering argument that fits in a paragraph.
- **It would need wrapping anyway.** Nothing outside our code carries `RB_NONBLOCKING`, so every push and pop on an audio path would go through a wrapper of ours to be annotated, and Clang's `-Wfunction-effects` would take the unannotated calls underneath on trust. Writing the sixty lines gives the compiler something to check.
- **Fixed capacity is the point, not a limitation.** `ReaderWriterQueue` allocates a new block when it runs out, on the thread that pushes. We want the opposite: a full queue refuses, and the caller decides what that means.
- **Four of the five types are ours regardless.** One queue from elsewhere and four primitives of ours would mean two conventions for naming, ordering and failure in the same layer.

The cost of this decision is ours to carry: lock-free code is where subtle mistakes live. That is why the stress tests are part of the deliverable rather than an extra.

### Details that follow from the shapes

- **`SeqlockValue` holds its value as 32-bit words, each its own atomic**, and copies in and out with `memcpy`. Reading a plain struct while another thread writes it is a data race by the letter of the language however the processor behaves, and ThreadSanitizer says so.
- **The ordering rides on the word accesses, not on standalone fences.** The usual formulation of a seqlock fences the block of word accesses as a group; GCC's ThreadSanitizer cannot model `atomic_thread_fence` and refuses to compile it (`-Wtsan`). Releasing and acquiring each word gives the same guarantee and can be checked, which for a primitive this delicate is worth the stronger stores on a weakly ordered processor.
- **`RetireQueue` refuses rather than dropping.** When it is full, `tryRetire()` leaves the caller owning the object, because the alternative, letting it go out of scope on the audio thread, is the one thing the class exists to prevent. `retireRefusedCount()` says whether it has ever happened.
- **`RealtimeLogger` does not read the clock.** Records carry a sequence number and the drainer timestamps them. A clock read is a call out of our code that a real-time checker has to take on trust, and with a drain every frame the difference is not worth it. The sequence numbers also show where a full ring dropped a record.

## Consequences

- The layer table in [architecture.md](../architecture.md#layers) gains a row at the bottom, and the layering check gains `reverblabr-rt` and a `rt-qt` self-test case.
- Blocks publish their read-only outputs through these slots, the runtime uses the queue and the retire queue, and the telemetry pipeline is built on the snapshot types.
- The primitives are tested against 10⁷ operations under ThreadSanitizer and RealtimeSanitizer. That suite is slow by design and belongs in the sanitizer CI job, not in every build.
- If a future need outgrows a fixed-capacity single-producer queue (several plugin hosts feeding one drain, say), this record is the one to supersede, and moodycamel's is the first thing to reach for.

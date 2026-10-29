<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0022: A block's settings may say what it is prepared for; the runtime prepares a fresh block when they change, and its lanes wait until it is filled

- **Status:** Accepted
- **Date:** 2026-10-29

## Context

Frequency Raising works on frames of a short-time Fourier transform, and its latency is one frame. A move from a low source needs a long frame to tell the harmonics of a low voice apart: 4096 samples at 48 kHz, about 85 ms. A move from 250 Hz up does as well with 2048, about 43 ms. The block used the long frame always, so every listener paid 85 ms for what only a low move needs.

What stood in the way:
- **A block's latency may change only at `prepare()`** (`dsp::Block::info()`), and the compiled graph lines every other path up with the latency it read when it was compiled.
- **The runtime prepares a block again only when the rate or its layouts change** (`SessionRuntime`'s `prepareBlocks()`, `Instance::wants()`). And never in place: a block the audio thread may be running is replaced by a fresh one with the same settings.
- **A move's range is an automatable number.** It reaches the block through the parameter queue, so a dragged range costs no replan, and nothing would line the graph up with a new latency.
- **A fresh block starts empty.** Its first latency's worth of samples is silence, and the sound then comes back with a step: a click. The node dips the lanes an edit is heard through (10 ms down, the swap, 10 ms up), but the fade back up ended long before a 2048-sample silence did. Measured on the change of frame: −53 dB above 10 kHz where the block's own changes measure −120.

## Decision

**A block may say what its settings need it prepared for.** `dsp::Block::preparedShape()` is a number, 0 for nearly every block, that stands for a choice `prepare()` makes from the settings about memory and latency. It must follow from the settings alone. Frequency Raising's is 1 while a move in use starts below 250 Hz (`FrequencyRaising::kLongFrameBelowHz`), and its `prepare()` then takes the long frame. A move switched off still counts, so switching it on and off to compare does not move the other ear in time; a move taken out of use does not.

**The runtime treats it as one more thing a block is prepared for.** `Instance::PreparedFor` holds it beside the rate and the layouts. `SessionRuntime::setParam()` asks the block after every setting: the number still goes through the queue as before, and when the shape is no longer the one the block was prepared for, the answer is `ParamEffect::NeedsReplan`. The engine then updates the session as it does for a structural parameter. `prepareBlocks()` replaces the block with a fresh one carrying its settings, the graph is compiled around its new latency, and the other paths are lined up with it. A drag replans only when it crosses the line, not on every value.

**A lane over a fresh block waits until the block is filled.** The runtime marks an update whose changed blocks include one it made afresh (`pw::SessionUpdate::refilled`). The node then asks for the fade back up with a hold of the new graph's latency (`FadeRequest::holdFrames`): the lanes stay at the gain they were swapped in at, silent, until every fresh block and every new line in the graph has been fed its delay, and only then ramp. The change is heard as a fade out, a gap as long as the new latency, and a fade in. Measured on the change of frame, both ways: −105 dB above 10 kHz.

**The panel works the delay out from the settings**, with the block's rule (`FrequencyMoves.qml`), so it says what the delay will be before the session runs and while a move is dragged.

## Consequences

- Frequency Raising's ear is held back 43 ms instead of 85 for the settings it starts with, and for every move from 250 Hz up.
- A move dragged across 250 Hz while the session runs is a gap of about 50 or 100 ms on its lanes. That is a setting being changed, not something played, so a gap is acceptable where a click is not.
- The hold applies to every block the runtime makes afresh, a source given another layout as well. It is the graph's latency, so a session with none waits not at all.
- Any block can now have a latency that follows a setting, at the cost of a replan when the setting crosses a line. A setting that would move the shape on every value of a drag must not use this: each change is a recompile and a gap.
- Outside the runtime, a block keeps the frame it was prepared with until somebody prepares it again; its moves go on working at that frame.

## Alternatives considered

- **Making the move's ranges non-automatable**, so that every change replans. Every value of a drag would then recompile the graph and dip the lanes.
- **A separate "lowest sound" choice**, non-automatable, that picks the frame. One more setting for a listener to get right, and a move set below it would quietly be heard at the wrong frame.
- **Keeping both frames running and crossfading inside the block.** It would avoid the gap, but it costs a second mover while the change lasts, the latency the graph lines up with would still change only at a recompile, and lines held back elsewhere for the old latency would start empty anyway.
- **Fading the fresh block in by itself after its latency.** It would cover the block but not the compensation lines the new graph makes for the paths beside it, which start empty for the same reason. Holding the lanes in the node covers both.

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0031: The Model block, with the model in a process of its own beside the session

- **Status:** Proposed
- **Date:** 2026-11-02

## Context

The Model block takes a folder that holds a trained model. Sound goes into the block, the model changes it, and the result comes out, while the session runs. Four jobs are wanted: voice conversion, noise and voice cleanup, plain audio in and audio out, and speech through a language model (speech to text, a language model, text to speech). The formats are ONNX and GGUF. A model that is slow or crashes must not break the session or the other lanes.

**Sound leaves this block only after the model has processed it.** That is the owner's rule, and it decides the rest: the block is silent in every other case, the input is never passed on, and a stop in this block's sound is correct behaviour. Other blocks' promise that the sound never stops does not hold here.

Four facts shape the design.

**A model's time is not ours to bound.** One run can take from a fraction of a millisecond to seconds. It depends on the model and the machine, and nothing about a file says which. The real-time rules forbid such work on the audio thread.

**PipeWire waits for a node.** ADR-0014 measured it: a host that stops holds up everything after it. So a model must not sit inside any node's cycle, not even in a host process of its own, unless the node's audio thread never waits for the model.

**A format does not say how to call a model.** ONNX and GGUF hold tensors and operations. Neither says what the inputs mean, the sample rate, how long a frame is, or which outputs feed the next run. That knowledge belongs to a model's family, so a folder has to be matched to a family before anything can run.

**A model file comes from strangers.** A person downloads a folder and points the block at it. Files like these have been used to run code before, and the parsers that read them are large.

## Decision

### The host process

**A program of its own, `reverblabr-model-host`, one process for each Model block.** It follows ADR-0014's pattern: one process an instance, no Qt, a memfd and a socket, a magic number and a layout version, `PR_SET_PDEATHSIG` so it never outlives the engine, and a pidfd to learn of its exit at once. Two things differ from the plugin host, and both are deliberate.

- **It is not a PipeWire node.** The block stays inside the session's node and runs in its cycle like any other block. It only copies samples to and from rings in shared memory. It never waits. So the session is not split, nothing is relinked, and PipeWire does not know the host exists. A host that crashes, hangs or runs late changes nothing in the graph.
- **It is not `reverblabr-plugin-host` with one more format.** That program links PipeWire, lilv, suil and X11, and starts with every plugin. ONNX Runtime and what it brings in would load with every plugin too, and the model host needs none of the plugin host's libraries.

Only the host links ONNX Runtime. The engine, the daemon and the window never load it, and never parse a model file.

### The protocol

A memfd holds a header and four rings, input and output for two channels. Each value has one writer.

| Value | Writer | Meaning |
|---|---|---|
| `written` | block | input frames written, counted from the start |
| `wetFrom`, `wetEnd` | host | the output frames that hold the model's sound, by the same count |
| `phase`, `reason` | host | starting, loading, ready or failed, and why |
| `load`, `device`, `badRuns` | host | a run's time as a share of the sound it makes, what ran it, and how many runs gave back what is not sound |
| `beat` | host | moves on every turn of the host's loop |
| `resetEpoch` | block | the signal path was cut, so start clean |

**Frame numbers tie it together.** Output frame `n` is the model's answer to input frame `n`, so a late or skipped frame is a gap in a count and never a shifted sound.

**The host polls `written` every millisecond.** Waking it would take a system call on the audio thread, which the real-time rules forbid.

**The socket carries no messages.** Its only job is to close: when the engine's end goes, the host exits, even in the middle of a run. A refusal is a `reason` in the memory. Nothing that owns memory crosses at run time, so there is nothing to parse in the engine.

**The block trusts nothing in the memory.** It keeps its own copy of the capacity and the channels, clamps the counts to what it has written, and masks every index. It reads floats and checks them: a sample that is not finite is silence, and the rest are capped at +12 dB.

### Latency

The block's latency is fixed when it is prepared and is the same at every quantum:

```
latency = one hop + what the path delays + spare time
```

- **One hop** is what has to arrive before a run can start, at the session's rate.
- **What the path delays** is the model's own look-ahead (from the family or the configuration) and the delay of the resamplers, if the model's rate is not the session's. The host drops that many samples from the front of its output, so output frame `n` lines up with input frame `n`.
- **Spare time** is the setting, 20, 50, 100 or 250 ms, 50 by default. It has to cover one cycle or one run, whichever is longer, and the fade below. A cycle of 2048 samples fits in the default at 48 kHz.

The value is reported as `BlockInfo::latencySamples`, so compensation lines it up like any other block's. A folder decides the hop, and a block has to know its latency in `prepare()` while a model can take a minute to load. So the block asks the host once, when the folder is set: `reverblabr-model-host --inspect <folder>`. That reads the description files and the head of the model, and it loads nothing. `preparedShape()` follows from the answer, so a new folder or setting gives a fresh block and a new graph (ADR-0022).

A block with no usable folder has no latency. If the session's cycle is longer than the spare time allows, the block cannot keep up at any speed, and the panel says so and asks for more.

### Silence in every other case

**Only the model's output leaves the block.** There is no dry path in it: the only samples it writes are the rings', times a gate, and everywhere else it writes zeros. The block is silent:

| When | The card says |
|---|---|
| It is switched off with the tile's switch | Off |
| No folder is chosen | No model |
| The model is loading, or the session has just started | Loading |
| A folder is refused, or a model cannot be loaded | Failed, and why |
| A cycle's sound is not ready in time | Cannot keep up |
| The host crashed, hung or is restarting | Restarting, then Failed if it keeps on |
| The model gave back what is not sound | Bad output |

**Off means silence through a declared property.** Every other block in the middle of a lane compiles to a pass-through when it is switched off. This one sets `BlockInfo::silentWhenOff`, which makes the compiler treat its dry signal as silence, as for a source or an output. So the fade of a bypass runs to silence and rises from silence, and the graph needs no knowledge of this block.

**A plugin's bypass does not apply.** A hosted plugin that crashes is relinked around (ADR-0014), and the sound goes on without it. This block has no node to link around, and no path that carries the input past it.

**A miss is silence for that stretch and nothing else.**

- A cycle is **healthy** when the rings hold this cycle's sound and 5 ms more. Those 5 ms are the guard.
- When a cycle is not healthy, the gate falls to zero over what the guard holds, so a stop does not click. The fade is made from the model's own sound. It never brings in the input.
- The gate rises over 5 ms in the first healthy cycle after. There is no wait: a one-off miss costs only what was missed. The card keeps saying Cannot keep up for two seconds, so it can be read.
- A host more than the latency behind skips to the present, because what it would make is already too late. A stateful model carries on from there.

**A crash** is the same case: the rings stop filling and the gate falls. The session never notices. The host is started again after a pause. After three crashes in a minute it is left alone and the block says **Failed**, with a Try Again button. **A hang** looks the same to the block. A `beat` still for ten seconds while sound goes in is killed and counted as a crash, and a load gets two minutes.

### How a folder is recognised

The host looks at the folder itself, without loading anything, in this order. The first match decides.

1. **`reverblabr-model.json`.** A person or a packager says what the model is. It always wins.
2. **The names of an ONNX file's inputs**, read by a small bounded reader of our own.
3. **File names and magic numbers**, such as `*.gguf` or a three-file set.

Otherwise the folder is refused, with the reason named: nothing to load, a file of a kind that can run code, an ONNX file that needs a configuration, a file too large, and so on.

**Each family is an adapter**, which turns a stream of sound into the calls that family expects.

| Family | Job | Recognised by | Stage |
|---|---|---|---|
| Configured | audio in, audio out | `reverblabr-model.json` | 1 |
| DeepFilterNet, one streaming graph | cleanup | inputs `input_frame`, `states`, `atten_lim_db` | 1 |
| GTCRN streaming | cleanup | inputs `mix`, `conv_cache`, `tra_cache`, `inter_cache` | later; needs a short-time transform in the adapter |
| DeepFilterNet, three graphs | cleanup | `enc.onnx`, `erb_dec.onnx`, `df_dec.onnx` | later; needs the band features in the adapter |
| RVC export | voice conversion | inputs `phone`, `phone_lengths`, `pitch`, `pitchf`, `ds`, `rnd` | 2 |
| Speech chain | speech through a language model | a `*.gguf` language model, a speech to text model and a voice | 3 |

A family that is recognised and not built yet is refused with its own reason, so a person learns what the folder is and that this version cannot run it.

**The configuration file for a generic model:**

```json
{
  "format": 1,
  "file": "model.onnx",
  "sampleRate": 48000,
  "channels": 1,
  "frameSize": 480,
  "hopSize": 480,
  "latencySamples": 0,
  "input": "audio",
  "output": "audio",
  "state": [{ "input": "state_in", "output": "state_out" }],
  "constants": { "strength": 1.0 },
  "threads": 1,
  "licence": "LICENSE.txt"
}
```

`file`, `sampleRate`, `channels`, `frameSize`, `input` and `output` are required. A run takes `frameSize` samples and gives back `hopSize` or `frameSize` samples, of which the last `hopSize` are used. `state` pairs are filled with zeros, then each run's output feeds the next run's input. `constants` are numbers for inputs that do not change. A stereo model is told `channels: 2`. A mono model on a stereo lane runs twice, once for each side, or once on the two sides mixed, which is the Mono setting.

### The four jobs, by stage

**Stage 1, built now.** The block, the host, the folder, the card and the tile, the configured family and the one cleanup family that can be tested here. ONNX through ONNX Runtime.

**Stage 2, voice conversion.** The same host and rings. The adapter runs a content encoder and a pitch tracker, then the converter, in overlapping windows, so a run is long and the spare time grows with it. A voice is a folder with its own licence. A panel for choosing the pitch shift comes with it.

**Stage 3, speech through a language model.** Whisper, a language model and a voice are three engines (whisper.cpp, llama.cpp and Piper, all packaged in Fedora). The chain cannot be real time: the output is seconds late and is not a changed version of the input, so it is not lined up with anything and the block does not report it as latency. It needs a card of its own, a separate host mode, and GGUF through those libraries. The panel offers none of this until then.

### Models as untrusted input

- **Nothing runs from a folder.** Custom operator libraries are never registered. Pickle files and the formats built on them (`.pt`, `.pth`, `.ckpt`, `.pkl`) are never opened, and a folder that holds only those is refused and told why. A folder's Python is not looked at.
- **Bounds first.** The configuration is at most 64 KiB, with bounded nesting, rates, channels, frames and state sizes. A model file is at most 2 GiB, checked before it is read, which is the most the format can hold.
- **The reader of our own** checks every length against the file and skips what it does not need. It refuses external data whose path is absolute or leaves the folder. An operator the runtime does not ship is an error when the model loads, as no library is ever registered to supply one.
- **Shapes are checked against the configuration when the model loads**, and every output's size is checked before it is copied.
- **In the process:** `PR_SET_NO_NEW_PRIVS`, no core files, and a limit on its address space. Reading a folder is done by that same program, so a bad file breaks one short process and no more.

The process runs with the person's rights. It is a wall against faults, not a sandbox against an attacker who has found a hole in ONNX Runtime. Stronger confinement is a later decision.

### What the program does not do

Models are never bundled, and the program never downloads one. It reads the folder it is given. If the folder has a licence file, the card names it and offers to open it. Terms are the person's to read, and the program does not judge them.

### Execution providers

CPU always works. The host takes a GPU provider when the installed ONNX Runtime offers one and it accepts the session, and the card says which ran. Fedora 44's package is built with CPU only, so that path is written and has not been run.

### Optional at build time

As for RNNoise (ADR-0016): `RB_BUILD_MODEL_BLOCK` is on when `libonnxruntime` 1.20 or later is found, and turning it on without it stops the configure with the package to install. A build without it has every other block and no `effect.model` in the registry.

### Beta

The block ships marked as a beta. The mark is one flag on the block's row in the presentation table, and everything a person sees follows from it. It comes off when the block has been used in earnest.

## Consequences

- A crash, a hang or a slow model costs silence on this block's lane and a word on the card, and no change in the graph or in any other lane.
- A person who wants to hear the original while a model loads or fails needs a lane of their own around the block. The block will not provide one.
- The block adds latency once it has a model, by default a hop plus 50 ms. Noise cleanup for a live call may want the 20 ms choice, which then asks for a quantum of about 700 frames or less at 48 kHz.
- Output frames are tied to input frames, so one block's output does not depend on the quantum while the model keeps up.
- The host polls. A running Model block costs about a thousand wake-ups a second in its host, even in silence.
- Inspecting a folder blocks the engine thread for as long as the short process takes, a few milliseconds usually and five seconds at most. A session with such a block takes that much longer to open.
- A model at the edge of what the machine can do is heard as gaps. That is the rule working, and the card says so.
- Sessions with this block do not open in a build without it, as with any block a build lacks.

## Alternatives considered

- **Fading to the plain input when the model fails.** The first version of this design did: the sound never stopped, and the block kept a delayed copy of the input to cross to. The owner ruled it out. A block that may pass its input on cannot promise that what leaves it was processed, and that promise matters more for a voice or a cleanup chain than a gap does.
- **A format of `reverblabr-plugin-host`, run in the node's cycle.** One program fewer. But either the host's audio thread waits for the model, which stops the graph behind a slow model (finding 2 of ADR-0014), or it does not wait, and then it is this design with a graph split and a relink added, and with a gap in the sound when the host dies.
- **ONNX Runtime in the engine, on a worker thread.** No process to manage, and the fastest handover. But a crash in the runtime or a model takes every session with it, and the engine would parse untrusted files. The owner's requirement rules it out.
- **A futex or an eventfd to wake the host.** Lower latency and no polling, but a system call on the audio thread in every cycle.
- **Latency that follows the quantum.** Shorter at small quanta, but every path around the block would be worked out again when the quantum moves, and the output would depend on how the input was cut.
- **Knowing the hop only once the model has loaded.** It would need a latency that changes seconds after a session starts. The inspection costs little and gives the same answer sooner.
- **Reading the folder in the window.** It would put a parser for untrusted files in the window's process, and the panel would disagree with the block about what the folder is. The window runs the same `--inspect`.
- **soxr for the rates.** It is already a dependency, but its streaming delay moves at the start of a stream, and the host needs an exact number. A small polyphase resampler of ours has one.

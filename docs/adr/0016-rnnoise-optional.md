<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0016: Speech Denoise on the distribution's RNNoise, built only where there is one

- **Status:** Accepted
- **Date:** 2026-09-28

## Context

The Speech Denoise block clears noise from speech with a trained model instead of a learned noise profile. The original plan named **RNNoise** (BSD-3-Clause; its model data CC0), vendored into the tree, with DeepFilterNet as a later "high quality" option.

What the platforms have:

- **Fedora 44** packages RNNoise 0.2 with its model built in: `rnnoise-devel`, a `rnnoise.pc` for pkg-config, `librnnoise.so.0`.
- **Kubuntu 26.04** does not. Ubuntu's `librnnoise-dev` (0.2+ds-1) is only in the release after it.

Vendoring RNNoise would bring the first third-party C into the tree, and a large one: the 0.2 model is several megabytes of generated C arrays. It would also bring its SIMD dispatch, its warnings under `-Werror` and the sanitizers, its REUSE entries, and every later model update. ADR-0015 turned down vendoring a much smaller library for the same reasons. Kubuntu is the secondary platform (ADR-0004) and is not tested at the moment.

## Decision

- **The distribution's RNNoise, through pkg-config** (`rnnoise >= 0.2`), linked into the blocks library only.
- **Optional at build time.** `RB_BUILD_SPEECH_DENOISE` is on when RNNoise is found and off otherwise. Turning it on without RNNoise stops the configure with the package to install. A build without it has every other block, no `advanced.speechdenoise` in its registry, and so none in the library. CMake's feature summary and a status line say why.
- **The dependency lists say the same.** `tools/deps/fedora.txt` installs `rnnoise-devel`. `tools/deps/kubuntu.txt` says why there is nothing to install. `tools/check-deps.sh` reports RNNoise as optional.
- **Two library calls on the audio thread**, `rnnoise_process_frame()` and `rnnoise_init()`, each wrapped where `-Wfunction-effects` is quietened for that call alone. The rtsan preset and the allocation trap check them at run time. The state is made by `rnnoise_create()` in `prepare()`.
- **48 kHz only.** At another rate the block passes the sound through at the same latency and says so. It does not resample on the audio thread.

## Consequences

- On Fedora the block is there with nothing to vendor or keep up to date. A security or model fix comes with the distribution's update.
- On Kubuntu 26.04 the block is missing until Ubuntu packages RNNoise. A session saved on Fedora with a Speech Denoise in it does not open there: the block type is unknown, as for any block a build lacks. Once a Kubuntu release packages `librnnoise-dev`, adding it to `kubuntu.txt` is all it takes.
- Two builds of the same version can offer different blocks. The catalogue is the engine's, so the interface never offers a block the engine cannot make.
- The model is whatever the distribution ships. The tests hold the block to its behaviour (noise down, speech kept, the latency it reports), not to exact samples, so a model update does not break them unless it gets worse.

## Alternatives considered

- **Vendor RNNoise**, as the original plan said: the block everywhere, at the cost above. It stays possible if Kubuntu users need the block before Ubuntu packages it.
- **Load `librnnoise.so` with `dlopen()` at run time**: one binary for both, with the block appearing when the library is installed. But it needs a header of our own for the API, a declared symbol version, and a failure path in the middle of a session. It saves nothing on Fedora, where the package is there anyway.
- **DeepFilterNet** (`libdf`) instead: better on changing noise, but it is not packaged by either distribution and its C API comes from a Rust build. It stays a later option for this block, not the base.

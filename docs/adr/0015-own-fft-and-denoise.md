<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0015: Our own real FFT, and our own spectral denoiser rather than libspecbleach

- **Status:** Accepted
- **Date:** 2026-09-28

## Context

The Denoise block learns a noise's spectrum and takes it out of what follows. That is a short-time Fourier transform on the audio thread: a real FFT of a fixed size, forwards and back, a few hundred times a second. The FIR Filter and the Parametric EQ's live spectrum need the same transform.

The original plan named two libraries:

- **libspecbleach** for the denoiser (LGPL-2.1). It is not packaged for Fedora 44 or Kubuntu 26.04. More to the point, it does its transforms with **FFTW**, which is GPL. FFTW in the process would make the program GPL as distributed, and ADR-0005 keeps Reverblabr MPL-2.0 and combinable with proprietary libraries. So libspecbleach cannot be taken as it is.
- **PFFFT** (BSD-like) or pocketfft for the FFT. The licence is fine. But either would be the first third-party C vendored into the tree, with its own REUSE entries, warnings to silence under `-Werror` and the sanitizers, and SIMD paths to keep building on both platforms.

What the blocks need is modest: one real transform of a power-of-two size between 512 and 8192, allocation-free after `prepare()`. They do not need the fastest transform on the machine.

## Decision

**`rb::dsp::RealFft`** (`engine/dsp/fft.h`) is ours:
- a radix-2 complex transform of half the length, with the step that turns it into a real one;
- twiddles and the bit-reversal table made in `prepare()`, in double and stored as float;
- nothing allocated, locked or waited on in `forward()` or `inverse()`;
- held to a DFT computed in double by its definition, at every size from 4 to 2048.

Every block that needs a spectrum uses it: Denoise now, FIR Filter and the live spectrum tap next.

**The denoiser is ours too** (`advanced.denoise`). It is a 2048-point STFT at 75 % overlap with square-root Hann windows, a learned per-bin noise power, and a gain per bin of
`max(floor, 1 − k · noise / signal)`, smoothed across time. It is the textbook spectral subtraction the libspecbleach design is built on, with the two controls the design asks for. The profile is saved with the block.

## Consequences

- No new dependency, no GPL anywhere in the process, and nothing to bundle when a distribution lacks a package.
- We own the transform's correctness. It is tested against the definition, and every user of it is tested through its own output.
- A faster transform can replace `RealFft` behind the same interface if a block ever needs one, for example a long FIR at 8192 points on a weak machine. PFFFT stays the first candidate.
- The denoiser has the simple algorithm's known character: some "musical noise" at high reduction, eased by smoothing the gains over time. libspecbleach's adaptive refinements (masking thresholds, transient protection) can be added to our own code if listening says they are needed.

## Alternatives considered

- **libspecbleach, built against a non-GPL FFT.** Upstream supports only FFTW. We would be carrying a patched fork of a library to get the algorithm this record describes.
- **Vendoring PFFFT now.** It is fine by licence. It was not needed for speed, and it costs a third-party tree and its upkeep. It stays the fallback.
- **KISS FFT.** BSD, and simpler than PFFFT, but slower than our radix-2 for the real case at these sizes, and it is still vendored C.

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0019: One 64-channel gammatone filterbank for both Hearing Compensation blocks

- **Status:** Accepted, amended when Hearing Compensation 16 was built (how a band's level is read)
- **Date:** 2026-09-28

## Context

The two Hearing Compensation blocks (one with 16 bands, one with 64) give each band of one ear a gain that depends on the band's level: soft sounds lifted a lot, loud ones little, under a ceiling. That needs a filterbank: split the ear's signal into bands on an auditory scale, measure each band's level every fraction of a millisecond, apply its gain, and put the bands back together without colouring the sound or delaying it much.

The spike measured four designs, offline, at 16 and 64 bands on the ERB scale from 100 Hz to 16 kHz. The numbers are in [docs/dev/spike-hearing-filterbank.md](../dev/spike-hearing-filterbank.md); the ones that decided it:

| | flat sum | follows a 0 → +30 dB slope (16 bands) | delay | cost, one ear, 64 bands | artefacts on a compressed vowel |
|---|---|---|---|---|---|
| Gammatone, 16 filters | ±0.67 dB | +6.2 dB off below 500 Hz | 4 ms | — | 76 dB under |
| **Gammatone, 64 channels** | **±0.36 dB** | **−0.6 .. +0.4 dB** | **4 ms (6 ms at 125 Hz)** | **2.2 % of a core** | **80–82 dB under** |
| Warped FIR (side-branch) | exact | +1.8 dB off | 0.6–2.2 ms (16), 2.3–9.0 ms (64): depends on frequency | 1.5 % | 76–84 dB under; music overshot full scale |
| WOLA FFT | exact | within 0.3 dB | 21 ms | 0.5 % | 56 dB under |
| Linkwitz-Riley tree | exact | +4.5 dB (16), +8.5 dB (64) off | 0 reported, 40 ms at 125 Hz (64) | 13 % | 85–87 dB under; overshot full scale |

## Decision

**The filterbank.** Both blocks use one bank of **64 complex gammatone filters** (Hohmann 2002): fourth order, 1 ERB wide, their centres evenly spaced in ERB-number from 100 Hz to 16 kHz (0.58 ERB apart). A channel's output is its analytic signal, so its level is |y|² with no rectifier or smoothing to add. The channels are put back together by delaying each one's real part so its impulse response peaks at **one common delay of 4 ms**, turning it so it peaks positive there, and weighting it so the sum is flat (±0.36 dB from 100 Hz to 16 kHz, at 44.1, 48 and 96 kHz). The design is made in `prepare()` for the rate; nothing is allocated after.

**The bands on it.**
- The **64-band** block gives each channel its own gain.
- The **16-band** block's bands are spaced 2.42 ERB apart on the same scale. Each channel's gain is the two nearest bands' gains, interpolated in dB over the ERB scale, and each band's level is the summed level of the channels nearest it. Sixteen filters of their own leak: their skirts are wide enough to put +6 dB where the gain asked for 0.
- The band centres are fixed at those spacings; both lists are in the spike notes.

**The delay each block reports** is 4 ms (192 samples at 48 kHz), whichever number of bands. The group delay is that from 1 kHz up and at most 2 ms longer at the bottom.

**Gains** change every 32 frames (two thirds of a millisecond at 48 kHz) and are ramped across those frames, per channel.

**Band coupling in the 64-band block.** A band's level is taken over its neighbours within ±2 ERB, weighted by a triangle (about one auditory filter wide). Compressed band by band, 64 bands flatten a third of a vowel's formant contrast; over ±2 ERB they keep 82 % of it, near what 16 bands keep, and the gains stay at 64-band resolution. The 16-band block takes each band on its own.

**The speeds.** The level detectors follow the level in dB and stop at the knee. Named by their attack and release on the standard 35 dB step:
- **Syllabic:** 5 ms and 70 ms.
- **Slow:** 50 ms and 1.5 s.
- **Adaptive:** two detectors. A slow one (325 ms and 1 s) sets the gain; a syllabic one takes over while it stands 8 dB or more above the slow one; and the slow one is held still through that, for at most 300 ms. A 100 ms burst is caught at once and the gain is back within 51 ms of its end; a sound that stays loud longer is taken as the new level.

**An audiogram on the bands.** Its values (at 125 Hz to 8 kHz) are interpolated linearly in dB over ERB-number to each band's centre, and held flat below the first and above the last point.

**A limiter at the ceiling** follows the resynthesis in both blocks. Per-band ceilings do not stop bands adding up past it, and the first block of a sudden sound goes out at the gain set before it.

## Amended when it was built

**Levels are read per channel, not per band.** "Each band's level is the summed level of the channels nearest it" turned out wrong once the gain depended on the level: a tone at one band's centre also passes through the channels between it and the next band, which took their gain from the next band's level (quiet, so its full soft gain). A tone at the average level came out 2.9 dB too loud, one at the loud level 4.6 dB, and a band's ceiling held a sudden loud tone back not at all.

- **Each channel reads its own level:** the power of the channels within 1 ERB of it at full weight and none beyond 2, scaled so that a sine at its centre reads its level.
- **Then the loudest reading within the coupling width** (the band spacing, 2.42 ERB, for the 16-band block), so every channel a tone goes through sees the tone's level.
- **Each channel's settings** are the two nearest bands', interpolated in dB over the ERB scale, as the gains were.
- The ceiling applies to each channel's level of the stretch it arrives in, with no attack time.

Tones now get their band's three gains within 0.19 dB, and a compressed vowel keeps all its formant contrast (101 %, where the band-by-band levels kept 86 %). The 64-band block reads levels the same way; whether its coupling stays the ±2 ERB triangle of the decision above or becomes the loudest reading within ±2 ERB is to be measured when it is built. The figures are at the end of [the notes](../dev/spike-hearing-filterbank.md#as-built-hearing-compensation-16).

## Amended when the 64-band block was built

**The 64-band block's coupling is the loudest reading within ±2 ERB**, the same reading as the 16-band block's with 2 ERB for the band spacing, rather than the triangle of the decision above. Measured on the block, with everything else the same:

| coupling | a loud tone's neighbours within 1 dB of its gain | tones' three gains | a compressed vowel's formant contrast |
|---|---|---|---|
| **loudest within ±2 ERB** | **out to 2.9 ERB either side, back to their own by 5.5** | **within 0.14 dB** | **100 %** |
| loudest within ±1 ERB | out to 1.7 ERB | within 0.53 dB | 94 % |
| none (the fine reading alone) | out to 1.2 ERB | within 1.16 dB | 86 % |

The wider hold is the fine reading's 1 ERB on top of the 2: a channel 3 ERB from a tone takes the reading of one 1 ERB from it, which reads the tone whole. It is what keeps every channel a tone goes through at the tone's gain, as the 16-band block found, and it keeps all of a vowel's formant contrast.

**It is set through control points**: up to 24, each with the four settings, and each setting of the 64 bands follows a monotone cubic (Fritsch and Carlson) through the points in log frequency, so the curves never overshoot the points; a −20 dB dip over half an octave at 4 kHz comes out within 0.58 dB of −20. **Its panel shows every band's gain and level**, so the band values the analysed block may publish went from 64 to 128 (ADR-0018, layout version 3). The figures are in [the notes](../dev/spike-hearing-filterbank.md#as-built-hearing-compensation-64).

## Consequences

- One filterbank to write, test and optimise, in `engine/dsp`, shared by both blocks. The per-channel values the analysed block publishes (up to 128 floats since the 64-band block: each channel's gain and its level) are this bank's channels, so a panel's graph shows the same bands in both blocks.
- Every Hearing Compensation block adds 4 ms. Two in series on one lane add 8; side by side on two lanes joined by Join L/R, the session is 4 ms late and the compiler lines the ears up.
- 2.2 % of a core per block at quantum 256 on this machine, in scalar code. Four or eight channels at once in SIMD is the saving if a slower machine needs it, and the reason the channels are laid out as arrays per stage.
- The sum is not exactly flat: ±0.36 dB of ripple with every gain equal, which is under what anyone can hear and under the ±0.5 dB asked for, but a null test of the block at 0 dB against its input will not null. Tests hold it to the flat-sum figure instead.
- The spike's prototype goes when the 16-band block lands (`spikes/README.md` records it).

## Alternatives considered

- **Sixteen gammatone filters for the 16-band block.** A quarter of the cost, but ±0.67 dB of ripple and +6 dB of leakage into bands whose gain is lower. Rejected on accuracy.
- **The side-branch (a frequency-warped FIR).** Exactly flat, the least delay and the cheapest. Its delay depends on frequency by a factor of four (0.6 ms at 4 kHz, 2.2 ms at 125 Hz with 16 bands; 2.3 to 9 ms with 64), so no single figure lines it up with another path. It redesigns its filter from the block before, so a sudden loud onset gets the old gains: music overshot full scale by 2.7 dB under −6 dBFS ceilings. It would need 128 taps and more analysis to resolve 64 bands at the bottom. It stays the fallback if 4 ms proves too much for a live use.
- **A WOLA FFT.** Cheapest per band, exact, but 21 ms of delay at the frame that gives the low bands enough bins, and 20–25 dB more artefacts than the rest when the gains move at syllabic speed.
- **A Linkwitz-Riley crossover tree.** Exact and without a reported delay, but the phase delay reaches 40 ms at the bottom with 64 bands, a slope leaks as much as 8.5 dB, and 64 bands cost 13 % of a core per ear. Right for the Multiband Compressor's three bands, wrong for sixteen or sixty-four.

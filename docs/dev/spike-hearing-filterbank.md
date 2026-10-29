<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Spike: a filterbank for the hearing blocks, the evidence

Measurements behind [ADR-0019](../adr/0019-hearing-filterbank.md), made for the hearing blocks. Everything is offline: no PipeWire, no D-Bus, one channel in and one out, through the DSP test kit.

- **Machine:** Fedora 44, AMD Ryzen 9 9950X3D2 (16 cores, 32 threads). The cost figures are from the `release` build, the best of three runs; every other figure is deterministic and the same in `debug` and `release`.
- **Prototype:** `spikes/hearing-filterbank/`, deleted once Hearing Compensation 16 had landed. Each candidate splits a channel into bands on the ERB scale from 100 Hz to 16 kHz, lets a control set every band's gain each 32 frames (two thirds of a millisecond at 48 kHz), and puts the bands back together.
- **Compression**, where a measurement needs it: per band, the band's gain for soft sounds from a sloping loss (0 dB up to 500 Hz, +10 dB at 1 kHz, +20 at 2 kHz, +30 from 4 kHz), 3:1 above −70 dBFS, under a ceiling at −6 dBFS in the band.
- **Fixtures** are made by the test: a voice-like buzz with formants in syllables, and chords over a bass line with a cymbal-like noise on the beat. Nobody's recording.

The block as built is measured by its own tests rather than the prototype; see [As built](#as-built-hearing-compensation-16) at the end.

## The candidates

| Candidate | What it is |
|---|---|
| **gammatone** | Hohmann's (2002) analysis and resynthesis: per band a fourth-order complex gammatone filter (four complex one-pole sections), whose output is the band's analytic signal. To put the bands back, each band's real part is delayed so its impulse response peaks at one common delay (4 ms), turned so it peaks positive there, and weighted so the sum is flat. The filters are as wide as the bands are apart, never under 1 ERB. |
| **gammatone-64ch** | The same bank with 64 channels whatever the number of bands. With 16 bands, each channel's gain is the bands' either side of it, linear in dB over the ERB scale, and each band's level is the sum of the channels nearest it. |
| **warped-fir** | The side-branch: the audio goes through one frequency-warped FIR (all-pass delays with Smith and Abel's ERB coefficient, 0.76 at 48 kHz) of 32 taps for 16 bands and 128 for 64. A windowed FFT of the same warped taps is the analysis, and each control block the band gains become a new filter, crossfaded in over the block. |
| **wola-fft** | A short-time FFT, square-root Hann at 75 % overlap, the frame the power of two nearest 21 ms (1024 at 48 kHz), the bins grouped into bands. |
| **linkwitz-riley** | A tree of fourth-order Linkwitz-Riley crossovers between the band centres, each band passed through the all-passes of the crossovers below it so the sum is flat. |

## 1. The sum, every band at one gain

```
[measure] flat, gammatone, 16 bands, 48.0 kHz: -1.19 .. +0.15 dB (±0.67)
[measure] flat, gammatone-64ch, 16 bands, 48.0 kHz: -0.34 .. +0.37 dB (±0.36)
[measure] flat, warped-fir, 16 bands, 48.0 kHz: -0.00 .. +0.00 dB (±0.00)
[measure] flat, wola-fft, 16 bands, 48.0 kHz: -0.01 .. -0.00 dB (±0.01)
[measure] flat, linkwitz-riley, 16 bands, 48.0 kHz: -0.00 .. +0.00 dB (±0.00)
[measure] flat, gammatone, 64 bands, 48.0 kHz: -0.34 .. +0.37 dB (±0.36)
[measure] flat, warped-fir, 64 bands, 48.0 kHz: -0.00 .. +0.00 dB (±0.00)
[measure] flat, wola-fft, 64 bands, 48.0 kHz: -0.01 .. -0.00 dB (±0.01)
[measure] flat, linkwitz-riley, 64 bands, 48.0 kHz: -0.04 .. +0.00 dB (±0.02)
```

From 100 Hz to 16 kHz, 600 points. At 44.1 and 96 kHz every candidate is within 0.03 dB of its 48 kHz figure; gammatone-64ch is ±0.35 at both. Only gammatone does not sum exactly: its resynthesis is an approximation, and 16 filters 2.4 ERB apart ripple ±0.67 dB, over the ±0.5 dB asked for. The same bank with 64 channels ripples ±0.36.

How the ripple depends on the delay the bands are aligned to, and with 16 filters, on their width:

```
[measure] gammatone, 64 bands, aligned at 2 ms: ±3.97 dB
[measure] gammatone, 64 bands, aligned at 3 ms: ±0.46 dB
[measure] gammatone, 64 bands, aligned at 4 ms: ±0.36 dB
[measure] gammatone, 64 bands, aligned at 5 ms: ±0.31 dB
[measure] gammatone, 64 bands, aligned at 6 ms: ±0.28 dB
[measure] gammatone, 64 bands, aligned at 8 ms: ±0.25 dB
[measure] gammatone, 16 bands, aligned at 4 ms: ±0.67 dB    (5 ms: ±0.54; 6 ms and more: ±0.56)
[measure] gammatone, 16 bands, 2.00 ERB wide, 4 ms: ±2.16 dB
[measure] gammatone, 16 bands, 2.37 ERB wide, 4 ms: ±0.74 dB
[measure] gammatone, 16 bands, 3.00 ERB wide, 4 ms: ±0.32 dB
```

Below 3 ms the low channels' impulse responses have not peaked yet and cannot be lined up. Past 4 ms the gain is small.

## 2. Gains that differ from band to band

A sloping gain, 0 dB up to 500 Hz rising to +30 dB from 4 kHz, set band by band; the error is against the same gains interpolated between the centres, from 150 Hz to 12 kHz:

```
[measure] slope 0 -> +30 dB, gammatone, 16 bands: error -0.86 .. +6.18 dB
[measure] slope 0 -> +30 dB, gammatone-64ch, 16 bands: error -0.63 .. +0.39 dB
[measure] slope 0 -> +30 dB, warped-fir, 16 bands: error -0.61 .. +1.77 dB
[measure] slope 0 -> +30 dB, wola-fft, 16 bands: error -0.03 .. +0.27 dB
[measure] slope 0 -> +30 dB, linkwitz-riley, 16 bands: error -0.58 .. +4.52 dB
[measure] slope 0 -> +30 dB, gammatone, 64 bands: error -0.62 .. +0.40 dB
[measure] slope 0 -> +30 dB, warped-fir, 64 bands: error -0.13 .. +0.35 dB
[measure] slope 0 -> +30 dB, wola-fft, 64 bands: error -0.02 .. +0.18 dB
[measure] slope 0 -> +30 dB, linkwitz-riley, 64 bands: error -0.02 .. +8.46 dB
```

**Sixteen gammatone filters leak.** The wide high filters' skirts reach down into the low bands: at 150–500 Hz, where the slope asks for 0 dB, they gave +5 to +6 dB. The crossover tree does the same through its fourth-order slopes. Running 64 channels under the 16 bands' gains ends it: the gains move with the ERB scale and each channel is narrow.

A step: every band up to the one at 977 Hz at +20 dB, every band above at 0 dB. Filters that do not add in phase would cancel where the two gains meet, and dip under both:

```
[measure] +20 dB up to 977 Hz, 0 dB from 1335 Hz, gammatone, 16 bands: -0.96 .. +20.02 dB
[measure] +20 dB up to 977 Hz, 0 dB from 1335 Hz, gammatone-64ch, 16 bands: -0.07 .. +20.05 dB
[measure] +20 dB up to 977 Hz, 0 dB from 1054 Hz, gammatone, 64 bands: -0.59 .. +20.08 dB
```

The other candidates: −0.2 dB at worst. No dip worth the name anywhere.

## 3. Delay

What each would report, and the group delay it actually has:

| | reports | 125 Hz | 250 Hz | 1 kHz | 4 kHz |
|---|---|---|---|---|---|
| gammatone, 16 bands (16 filters) | 4.00 ms | 4.78 | 3.47 | 4.15 | 4.01 |
| gammatone-64ch, 16 or 64 bands | 4.00 ms | 6.04 | 4.61 | 3.99 | 4.00 |
| warped-fir, 16 bands | 1.90 ms | 2.24 | 2.22 | 1.89 | 0.57 |
| warped-fir, 64 bands | 7.54 ms | 8.96 | 8.88 | 7.55 | 2.26 |
| wola-fft | 21.33 ms | 21.33 | 21.33 | 21.33 | 21.33 |
| linkwitz-riley, 16 bands | 0 | 9.66 | 7.32 | 2.76 | 0.76 |
| linkwitz-riley, 64 bands | 0 | 39.85 | 30.72 | 11.60 | 3.19 |

The targets were ≤ 5 ms with 16 bands and ≤ 10 ms with 64.
- **WOLA** is out on this alone: 21 ms, and a shorter frame gives the low bands too few bins to be bands.
- **The crossover tree** reports no delay but has 40 ms of phase delay at the bottom with 64 bands, and costs a quarter of a core (below).
- **The warped FIR** is quickest, but its delay depends on frequency by a factor of four, so a sound's low part arrives milliseconds after its high part, and no single number describes it for lining paths up.
- **Gammatone's** delay is the alignment point from 1 kHz up, and only the lowest channels, whose impulse responses peak late, lag it by up to 2 ms.

## 4. Cost

Quantum 256 at 48 kHz, a syllabic compressor per band, two ears, release build, the best of three runs:

```
[measure] gammatone, 16 bands x 2 ears:        59.6 us a cycle, 1.12 %
[measure] gammatone-64ch, 16 bands x 2 ears:  230.3 us a cycle, 4.32 %
[measure] warped-fir, 16 bands x 2 ears:       29.4 us a cycle, 0.55 %
[measure] wola-fft, 16 bands x 2 ears:         55.7 us a cycle, 1.04 %
[measure] linkwitz-riley, 16 bands x 2 ears:   75.4 us a cycle, 1.41 %
[measure] gammatone, 64 bands x 2 ears:       238.8 us a cycle, 4.48 %
[measure] warped-fir, 64 bands x 2 ears:      157.9 us a cycle, 2.96 %
[measure] wola-fft, 64 bands x 2 ears:         56.3 us a cycle, 1.06 %
[measure] linkwitz-riley, 64 bands x 2 ears: 1405.8 us a cycle, 26.36 %
```

A block works on one ear, so one Hearing Compensation block on 64 gammatone channels is half of that: **2.2 % of a core**, within the 3 % the blocks are held to. The prototype is plain scalar code, one channel at a time; four or eight channels at once in SIMD is the obvious saving if a slower machine needs it.

## 5. Artefacts under fast compression

A steady vowel on 125 Hz whose level swings by 20 dB four times a second, like syllables, through the syllabic compressor. Whatever lands between the harmonics (45–80 Hz above each), more than the swing's own sidebands, is made by the filterbank changing its gains:

```
[measure] vowel, input: between the harmonics 110.6 dB under them
[measure] vowel, gammatone, 16 bands, syllabic: 75.8 dB under them
[measure] vowel, gammatone-64ch, 16 bands, syllabic: 80.0 dB under them
[measure] vowel, warped-fir, 16 bands, syllabic: 83.8 dB under them
[measure] vowel, wola-fft, 16 bands, syllabic: 55.9 dB under them
[measure] vowel, linkwitz-riley, 16 bands, syllabic: 84.6 dB under them
[measure] vowel, gammatone-64ch, 64 bands, syllabic: 81.8 dB under them
[measure] vowel, warped-fir, 64 bands, syllabic: 75.9 dB under them
[measure] vowel, wola-fft, 64 bands, syllabic: 56.3 dB under them
[measure] vowel, linkwitz-riley, 64 bands, syllabic: 87.0 dB under them
```

WOLA is 20–25 dB worse than the rest: a gain changed once a hop is a gain that steps across a frame, and the frame's circular convolution folds it back. Everything else leaves its products 75 dB or more under the sound.

Ten seconds each of speech and music, compressed syllabically over the sloping gain, peak and RMS out:

```
[measure] speech, gammatone-64ch, 16 bands, syllabic: peak -16.5 dBFS, rms -36.8 dBFS
[measure] music, gammatone-64ch, 16 bands, syllabic: peak -12.7 dBFS, rms -30.9 dBFS
[measure] music, warped-fir, 16 bands, syllabic: peak 2.7 dBFS, rms -27.8 dBFS
[measure] music, linkwitz-riley, 16 bands, syllabic: peak 2.5 dBFS, rms -23.5 dBFS
```

The warped FIR and the crossover tree let the music's cymbal hits through above full scale, although every band is under a −6 dBFS ceiling. The warped FIR redesigns its filter from the analysis of the block before, so an onset gets the old gains for a block, and the crossover tree's bands overlap enough to add up past the ceiling. Gammatone kept the same music 12 dB under full scale. All of them need a limiter at the output anyway (below, 7).

The listening files (input and every candidate, 16 and 64 bands, speech and music) are written by `RB_SPIKE_WAV_DIR`; they are not committed.

## 6. How the 64-band block keeps neighbouring bands together

A steady vowel whose formants stand 14 dB above the harmonics 1.5 ERB either side of them, compressed 3:1 with the level taken band by band, or over neighbours with a triangular weight:

```
[measure] formant contrast, input: 14.4 dB
[measure] formant contrast, gammatone 16 bands, level over ±0.0 ERB, 3:1: 13.3 dB (92 %)
[measure] formant contrast, gammatone-64ch 16 bands, level over ±0.0 ERB, 3:1: 12.3 dB (86 %)
[measure] formant contrast, gammatone 64 bands, level over ±0.0 ERB, 3:1: 9.5 dB (66 %)
[measure] formant contrast, gammatone 64 bands, level over ±1.0 ERB, 3:1: 10.0 dB (69 %)
[measure] formant contrast, gammatone 64 bands, level over ±2.0 ERB, 3:1: 11.8 dB (82 %)
[measure] formant contrast, gammatone 64 bands, level over ±3.0 ERB, 3:1: 13.2 dB (91 %)
```

Sixty-four bands compressed on their own flatten a third of each formant's contrast away, which is the spectral smearing speech suffers most from. Taking the level over ±2 ERB (a triangle two ERBs wide at half height, about one auditory filter) keeps 82 %, near what 16 bands keep, while the gains themselves stay at 64-band resolution for the loss's shape. ±3 ERB keeps more, but then the 64-band block compresses much like the 16-band one.

## 7. The speeds

The detectors follow the level in dB, and stop at the knee: a level under the knee changes no gain, so a pause is not a silence to climb back out of. Measured the standard way, on a 35 dB step at 3:1 (attack: until within 3 dB of where it settles; release: within 4 dB):

```
[measure] speed syllabic: attack 5 ms, release 70 ms
[measure] speed slow: attack 50 ms, release 1503 ms
[measure] speed adaptive, slow held 300 ms: attack 625 ms, release 1001 ms
```

What each does to the speech fixture's 10 ms levels, and to a 100 ms burst 30 dB over a steady sound (measured from the burst's second gain update, 0.7 ms in; the first block goes out at the gain the one before it set, whatever the speed):

```
[measure] speech's 10 ms levels, input: spread 11.5 dB
[measure] ... syllabic: spread 5.0 dB
[measure] ... slow: spread 7.9 dB
[measure] ... adaptive (plain dual front end, 8 dB): spread 7.2 dB
[measure] ... adaptive, slow held 300 ms (8 dB): spread 6.4 dB
[measure] burst of 100 ms, syllabic: 1-10 ms out at up to -10.1 dBFS; gain back within 2 dB 91 ms after
[measure] burst of 100 ms, slow: 1-10 ms out at up to -2.7 dBFS; gain back within 2 dB 1947 ms after
[measure] burst of 100 ms, adaptive (plain): 1-10 ms out at up to -10.1 dBFS; gain back within 2 dB 877 ms after
[measure] burst of 100 ms, adaptive, slow held 300 ms: 1-10 ms out at up to -10.1 dBFS; gain back within 2 dB 51 ms after
[measure] burst of 1000 ms, adaptive, slow held 300 ms: gain back within 2 dB 1301 ms after
```

- **Syllabic** evens speech out most (11.5 → 5.0 dB of spread) and lets a burst go at once, at the cost of pumping on music.
- **Slow** leaves speech's own dynamics mostly alone, but lets a burst's onset through 7 dB louder and holds the gain down for two seconds after it.
- **Adaptive** is two detectors, as in the dual front end: a slow one (attack 325 ms, release 1 s) sets the gain, and a syllabic one takes over while it stands 8 dB above the slow one. Plain, a 100 ms burst lifts the slow detector 14 dB and the gain takes 877 ms to come back. **Holding the slow detector still through a burst, for at most 300 ms**, makes that 51 ms, and a sound that stays loud longer than 300 ms is still taken as the new level (1 s burst: 1.3 s, like the plain design). A 12 dB threshold was tried too: it lets more of speech through the slow path (7.4 dB of spread), which is the opposite of what the fast path is for.

Two things these do not do, which the block has to: the first block of a burst goes out at the old gain (0.7 ms; up to −1.8 dBFS with Adaptive, −6.4 with Syllabic), and bands under their own ceilings can still add up past full scale (section 5). A look-ahead-free limiter at the ceiling, after resynthesis, catches both.

## 8. The bands, and an audiogram on them

Centres evenly spaced in ERB-number from 100 Hz to 16 kHz:

```
16 bands, 2.42 ERB apart: 100 198 324 488 701 977 1335 1800 2402 3183 4196 5509 7212 9421 12285 16000 Hz
64 bands, 0.58 ERB apart: 100 121 143 167 192 219 248 278 311 345 382 421 462 506 553 603 656 713 773 837
                          905 977 1054 1136 1223 1316 1415 1520 1631 1750 1876 2011 2154 2306 2468 2640
                          2823 3018 3225 3446 3680 3930 4196 4478 4778 5098 5438 5800 6185 6594 7030 7494
                          7987 8511 9069 9663 10294 10966 11681 12441 13250 14111 15026 16000 Hz
```

An audiogram's frequencies (125 Hz to 8 kHz) do not fall on either set, and should not have to: a value is given at each band centre by interpolating the audiogram linearly in dB over the ERB-number scale, and held flat below its first and above its last frequency. Where the audiogram's points fall among the 16 bands, the nearest centre is at most 1.1 ERB away (250 Hz → 198 Hz).

## As built: Hearing Compensation 16

The first block on this filterbank, Hearing Compensation 16, found one thing the spike could not: **with the gain depending on the level, the bands' levels have to be read per channel.** The spike's 16-band option took each band's level as the summed power of the channels nearest it and gave each channel the two nearest bands' gains. A steady tone at one band's centre then also goes through the channels between it and the next band, and they lift it by the next band's soft gain, since that band hears nothing. On the block: a tone at the average level came out 2.9 dB too loud and one at the loud level 4.6 dB, and a band's ceiling held nothing back.

So every channel reads its own level (the channels within 1 ERB of it at full weight, none by 2, scaled so a sine at its centre reads its level) and then takes the loudest such reading within the 16 bands' spacing, 2.42 ERB; its settings are the two nearest bands', interpolated in dB. Worked out from the channels' responses, a sine anywhere reads its level within 0.06 dB in every channel within 1.5 ERB of it. The block's tests, in `tests/unit/blocks/test_hearingcompensation.cpp` and `tests/unit/dsp/test_hearingbands.cpp`:

```
[measure] 64 gammatone channels at 48.0 kHz sum flat within 0.22 dB (worst at 161 Hz)
[measure] a sine reads its level within 0.06 dB in every channel within 1.5 ERB
[measure] every band at 0 dB: within 0.16 dB of the input from 125 Hz to 12 kHz
[measure] tones at the soft, average and loud levels of three bands: within 0.19 dB of the gain
[measure] a tone jumping 55 dB at 4196 Hz under a -15 dBFS ceiling: at most -14.83 dBFS in its band
[measure] a vowel's formant contrast, 14.4 dB in, 14.5 dB out at 3:1 (101 %)
[measure] 60 ms after a 100 ms burst 25 dB up: Adaptive 0.1 dB above the level, Slow 22.9
```

The formant contrast is **better than section 6's 86 %**, because the harmonics beside a formant now read the formant's level and get its gain. A minute of one ear at quantum 256 costs 1.34 % of a core in release, including the true-peak limiter the block adds after the bands.

## As built: Hearing Compensation 64

Hearing Compensation 64 reads levels as the 16-band block does, each channel its own reading and then the loudest within ±2 ERB, and gives each channel its own settings, from curves through up to 24 control points. Its tests, in `tests/unit/blocks/test_hearingcompensation64.cpp` and `tests/unit/dsp/test_hearingbands.cpp`:

```
[measure] 200 random curves through up to 24 points: 0 readings beyond their two points (worst 0.000 dB)
[measure] a -20 dB dip over half an octave at 4 kHz: 1 of 16 bands inside it, the deepest at -20.0 dB
[measure] a -20 dB dip over half an octave at 4 kHz: 5 of 64 bands inside it, the deepest at -20.0 dB
[measure] a -20 dB dip over half an octave at 4 kHz, set with points: tones across it within 0.58 dB of -20
[measure] a loud tone's neighbours (ERB away:gain dB): -5.8:30.0 -5.2:30.0 -4.6:24.8 -4.0:17.9 -3.5:13.2 -2.9:10.9 -2.3:10.1 -1.7:10.0 -1.2:10.0 -0.6:10.0 +0.0:10.0 +0.6:10.0 +1.2:10.0 +1.7:10.0 +2.3:10.1 +2.9:10.7 +3.5:12.9 +4.0:17.1 +4.6:22.6 +5.2:27.4 +5.8:30.0
[measure] bands within 1 dB of the loud band's gain out to 2.9 ERB either side
[measure] 64 bands at 0 dB: within 0.16 dB of the input from 125 Hz to 12 kHz
[measure] 64 bands: tones at the soft, average and loud levels within 0.14 dB of the gain
[measure] 64 bands: a tone jumping 55 dB under a -15 dBFS ceiling peaks at -14.64 dBFS in its band
[measure] 64 bands: a vowel's formant contrast, 14.4 dB in, 14.4 dB out at 3:1 (100 %)
```

The neighbours test is every band lifting soft sound 30 dB, average 20 and loud 10, a tone at the loud level at one band's centre over faint pink noise, and no noise floor: the bands near the tone take its loud gain, the far ones keep the noise's soft gain. Section 6's triangle over ±2 ERB kept 82 % of the vowel's contrast; the loudest reading within ±2 ERB keeps 100 %. With a coupling of ±1 ERB the neighbours are held out to 1.7 ERB, tones get their gains within 0.53 dB and the vowel keeps 94 %; with none, 1.2 ERB, 1.16 dB and 86 %. Both ears, two blocks, at quantum 256 in release: a minute in 1.59 s, **2.65 % of a core** (1.33 % an ear, as the 16-band block), measured at a load of 10.


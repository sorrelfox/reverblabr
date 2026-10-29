<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# From an audiogram to band settings

The listening blocks start from what one ear hears: its audiogram. **Fit from audiogram** turns that into each band's settings in one step, and one undo takes it back. This page says what goes in, which published rules are used, and what comes out. The code is `engine/dsp/audiogram.h`, `engine/dsp/fittingrule.h` and `engine/core/audiogramfit.h`.

Reverblabr shapes sound for listening. It is not a medical device and measures nothing about anybody's health; the audiogram is whatever the listener types in.

## One ear's audiogram

A block keeps one ear's audiogram among its own parameters, so each ear's block has its own.

- **Eleven points:** 125, 250, 500, 750, 1000, 1500, 2000, 3000, 4000, 6000 and 8000 Hz. Any of them may be left out.
- **At each point:**
  - whether anything was measured: nothing, heard, or no response (nothing heard even at the loudest level tried);
  - the quietest level heard, or for no response the loudest level tried;
  - optionally, the level at which sound becomes too loud.
- **The unit:** dB HL, typed in from an audiogram the listener already has, or dBFS, measured later through the very headphones the sound is played on.
- **The parameters** are `audiogramUnit`, then for each frequency `audiogram<Hz>` (the mark), `audiogram<Hz>Db`, `audiogram<Hz>TooLoud` and `audiogram<Hz>TooLoudDb`. They are numbers and choices, so editing them never makes the session be planned again, and they are hidden from the generic settings panel because the block's own panel draws the audiogram.
- **Between two points**, a level is taken on a straight line in log frequency. Below the lowest point and above the highest, that point's level carries on.

## The rules

Only published formulas that anyone may use are implemented. NAL-NL2 and DSL v5 are licensed and are not reimplemented.

- **Half gain** (Lybarger, 1944): half the loss, at every frequency.
- **NAL-R** (Byrne, D. & Dillon, H. (1986), *Ear and Hearing* 7(4), 257–265, from the National Acoustic Laboratories). The gain for sound of average loudness at each frequency is

  0.31 × the loss there + 0.05 × (the losses at 500, 1000 and 2000 Hz added) + a constant,

  where the constant is −17 dB at 250 Hz, −8 at 500, −3 at 750, +1 at 1000 and 1500, −1 at 2000 and −2 at 3000, 4000 and 6000. Below 250 Hz and above 6 kHz the nearest constant carries on.

  **This is NAL-R as published in 1986, without the NAL-RP extension** (Byrne, Parkinson & Newall, 1990). For a profound loss, where the losses at 500, 1000 and 2000 Hz add up to more than 180 dB, NAL-RP raises the averaged term to 9 + 0.116 × (that sum − 180). Without it, losses like N6 and N7 get less gain than NAL-RP would give them; those bands ask for more than the 50 dB maximum anyway and are marked. The 0.05 averaged term, the 0.31 slope and the constants at the six frequencies it lists (250, 500, 1000, 2000, 4000 and 6000 Hz) agree with the open-source NAL-R of the Clarity challenge toolkit (`clarity/enhancer/nalr.py`), which has the NAL-RP extension besides; the constants at 750, 1500 and 3000 Hz are the published table's.

Either rule's result is never below zero and **never more than the loss itself**. Without that bound, NAL-R's positive constants round 1 kHz would give an ear with no loss a decibel of gain.

## What a band is given

For each band of the block's layout (the layout is decided in ADR-0019), the fit works out four numbers on the audiogram's own scale:

- **Average:** what the rule asks for.
- **Soft** (15 dB below average sound): at least enough to put soft sound 5 dB above the quietest level heard there, and never less than the average gain.
- **Loud** (15 dB above average sound): no more than the average gain.
- **Ceiling:** 5 dB below the "too loud" level. Where none was measured it is taken as 100 dB HL, or 10 dB above the quietest level heard if that is higher. No soft, average or loud sound is given enough gain to go above it.

Average sound is placed at 45 dB HL in every band, roughly where ordinary speech falls on an audiogram; the block's Loudness offset then sets the overall level by ear. So in every band soft ≥ average ≥ loud: quiet sound is lifted a lot and loud sound little. In this first version the compression comes from lifting soft sound, and loud sound is held back only where the ceiling is near.

**The most gain a band may have** is 50 dB by default. Where the audiogram asks for more, the band is given that much and marked; where the nearest measured point was "no response", the band is marked too. The panel suggests Frequency Lowering for those bands, since more gain there is unlikely to help.

For a dBFS audiogram the same arithmetic runs on dBFS numbers. Its reference levels are placeholders until the listening check measures them.

## The band settings

A fit writes `band<N>SoftGainDb`, `band<N>AverageGainDb`, `band<N>LoudGainDb` and `band<N>CeilingDb` for bands 0 upwards, rounded to a tenth of a decibel, as one list edit. Settings that already have those values are left out, so fitting twice changes nothing the second time.

**In a Hearing Compensation block** the fit uses the block's own bands (ADR-0019's ERB-spaced centres; 16 for Hearing Compensation 16) through `core::fitHearingBlock()`. **Hearing Compensation 64** is set through control points, so the fit places a point at each frequency the audiogram has a level for, heard or not, fits it there exactly as a band at that frequency would be fitted, and switches every other point off; the 64 bands then follow the curves through the points. The block keeps its ceilings on the same scale as a dB HL audiogram, with average sound at 45, which it maps to −40 dBFS of a band's level. For a dBFS audiogram, average sound is put at that −40 dBFS and each ceiling is moved up onto the block's scale, 85 dB higher.

## Checked against the standard audiograms

**N1–N7 are checked** against table 1 of that paper, as reproduced in secondary sources. **S1–S3 are still to be checked against table 1**; if they differ, the reviewed tables are written again and the diff reviewed.

The tests fit the ten standard audiograms of Bisgaard, N., Vlaming, M. S. M. G. & Dahlquist, M. (2010), "Standard audiograms for the IEC 60118-15 measurement procedure", *Trends in Amplification* 14(2), 113–120: N1–N7, flat and gently sloping, and S1–S3, steeply sloping. What each rule gives them is kept in `tests/unit/dsp/fitting/nalR.tsv` and `halfGain.tsv`, so a change to the rule shows up as a diff to review. To write them again after a deliberate change:

```sh
RB_UPDATE_FITTING_TABLES=1 build/debug/bin/reverblabr-dsp-test --gtest_filter='FittingRule.TheStandard*'
git diff tests/unit/dsp/fitting/
```

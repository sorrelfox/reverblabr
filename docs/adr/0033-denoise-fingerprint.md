<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0033: Denoise learns a fingerprint of the noise, not its average

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

ADR-0015's denoiser learns a noise's mean power in every band over three seconds and turns a band down by `max(floor, 1 − k · noise / signal)`. That suits a steady noise. It does not suit the noise a USB sound card picks up from the computer it is plugged into, which is what people hear when the external source they record stops playing. Two recordings of one such card, with nothing playing into it, measured:

- **Level.** About −71 dBFS RMS, but with sharp drops every 7 to 50 ms, coming back over 1 to 4 ms, the deepest at −50 dBFS: ticks, mostly in the bass. Their peaks are 20 dB over the RMS.
- **Tones.** A hum wandering between 101.5 and 104 Hz, whose level swings by 10 dB; a series on 75 Hz, the main screen's refresh rate; a series on 1 kHz, the USB frame rate; a carrier at 19.2 kHz.
- **The same on both sides** (correlation 0.99): it comes in through the ground, not the inputs.
- **It drifts.** Recorded thirteen minutes apart, while the computer worked harder, the bass octaves were 4 to 5 dB louder, the middle 2 dB, the top the same.

Against the first block's own code, run offline over one recording after learning from the other: with its defaults, the silence came down 15 dB, but the loudest 10 ms only 8.5 dB, the ticks being far above the average it learned. Raised to 30 dB and a threshold of 10 dB, the silence was clean, and quiet music began to go from −60 dBFS (1 dB lost), some ten dB above the noise. The Dehum block looks for mains hum, and Declick did not find the ticks; Speech Denoise keeps speech and takes everything else, music too.

## Decision

`advanced.denoise` keeps its frame (2048 points, a quarter of one apart, square-root Hann windows), its latency, its controls and its profile in the session, and changes what it learns and how it decides.

- **Learning takes ten seconds.** Every band's level is measured twice: as it is in each frame ("now") and as the median of the last 75 ms of frames ("held", seven frames at 44.1 and 48 kHz). The first five seconds give each band two ceilings, the level the noise stays at or under in 99 % of its frames, now and held, each band pooled with the two beside it; they are counted in 1 dB steps, so that learning allocates nothing and finding a ceiling is a short walk down from the top. The second five seconds measure, octave by octave (150, 300 … 9600 Hz, and the rest), how far the noise alone goes past those ceilings at once, as a sum over bands of the decibels over a 3 dB margin, now and held; and where the noise's held level rests under them (the point a quarter of the way up its bands).
- **Removing asks two questions of every octave.** Is the octave more than the noise now (its sum beyond twice the most the noise gave, and 6 more)? Is it more than the noise held (beyond 1.5 times the most, and 1 more)? The first is strict because a noise's ticks give a lot of it; the second is not because a held level hardly strays, and what lasts over its ceiling is a sound, such as a quiet note under the ticks or a voice's weaker harmonics. **An octave that answers no to both holds the noise alone**, and all its bands go down by the reduction. In any other octave a band passes untouched once it is "Keep sound above noise by" over a ceiling the octave answered yes for, and goes down in full 6 dB under that. Gains rise at once and fall over 60 ms; both sides share one gain per band.
- **Octave by octave, not frame by frame.** A whole-frame decision let one of the computer's tones, swelling for a moment at 3 kHz, open the ticks in the bass, which sat just under their ceilings.
- **The fingerprint follows the noise up.** Each octave's ceilings follow the difference between where its held level rests now and where it rested while learning, over two seconds, up to 10 dB above what was learned and never below it: the hiss can quieten while the ticks do not. An octave follows only while three quarters of its bands are less than 6 dB over its ceilings, held: a noise grown is every band a little over, a sound is some bands a lot over. It follows while it is heard too, since a noise grown at once is heard until followed.
- **The profile is a new text, "v2"**: the rate, the length, both ceilings per band, both limits and the rest per octave. A "v1" profile, an average, holds none of that and is not read; the panel says to learn the noise again. Reduction now goes to 40 dB: turning a whole octave down is not the subtraction whose depth made noise twinkle.
- **The cost stays where it was.** The median of seven is a fixed network of thirteen orderings, the same for every band, which the compiler does eight bands at a time; following runs every fourth frame. A minute of stereo costs 1.0 % of a core in a release build, as the first block did.

## Consequences

- On the recorded card, learned from one recording and cleaning the other with music laid over it: at the default 20 dB the silence comes down 20 dB, its loudest 10 ms 20 dB too; quiet notes keep their level (−0.01 dB); a chord fading under the noise keeps its level down to −69.6 dBFS, about where it meets the noise. Learned on the quieter recording and cleaning the louder one, the silence comes down 19.5 dB and its loudest 10 ms 16.4.
- The block's first criteria hold: a fan learned and taken out of speech by more than 15 dB in its pauses, the speech's error against the clean voice down by more than 8 dB, its strong harmonics within 3 dB.
- A weak sound in an octave where the noise swings a lot has to stand out further to be heard there; the held question is what keeps a quiet sustained one.
- A tone just over the threshold loses its skirts, the bands beside it at a ninth of its power: up to 1.5 dB. Letting a passing band take its neighbours along kept that, and cost speech in a steady noise a decibel of error.
- A noise louder than it was while learning, ticks and all, is followed up only as far as its held level shows. At 30 and 40 dB the loudest ticks of the louder recording came down 18 to 19 dB. Learning again with the computer as it usually is, or after turning the card's gain, is the remedy, and the help page says so.
- Ten seconds of the noise alone are needed instead of three.
- A session's noise learned by the first block has to be learned again.

## Alternatives considered

- **The first block with stronger settings.** Clean silence at 30 dB and a 10 dB threshold, but quiet music lost from ten dB above the noise, and nothing to tell a person which settings their noise needs.
- **One decision per frame.** Clean on a noise learned and played back as it was, but a single tone of the computer's swelling opened every band near its ceiling.
- **A threshold per band at the noise's loudest, without octaves.** Better for speech, but a thousand bands each opening now and then by chance let a tenth of the noise through in a pause.
- **Subtraction, as the first block did, in the octaves that hold more than the noise.** As good for speech's error, but the voice's harmonics lost up to 3 dB and quiet music went earlier.
- **Following the noise down as well as up.** The louder recording learned and the quieter cleaned came out 7 dB worse: the hiss had quietened and the ticks had not.
- **A separate block for a sound card's noise.** Two blocks that learn a noise and take it out would leave people to guess which their noise needs; the fingerprint does both.
- **A trained model.** The ones there are keep speech and take everything else, which takes the music out with the noise.

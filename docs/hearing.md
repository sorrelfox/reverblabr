<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Hearing

One of the things I want Reverblabr to do well is help people who hear part of the sound poorly: someone who can't hear the high frequencies any more, who has a dip somewhere in the middle, or whose two ears hear differently. The blocks in the **Hearing** group of the library are for that. They work on one ear at a time, so each ear can be set up for the way it hears, and they can work from an audiogram if you have one.

Please keep two things in mind:

- **Reverblabr is not a medical device.** It shapes sound for listening and measures nothing about anybody's health. It doesn't replace an audiologist or a hearing aid. If your hearing has changed, have it checked.
- **Be careful with loud sound.** These blocks can add a lot of gain. Start with the volume low, especially with headphones, raise it slowly, and leave the **Output ceiling** in place.

## One ear at a time

Every block in the Hearing group except Join L/R has an **Ear** setting, Left or Right, at the top of its settings, and its tile shows an **L** (blue) or an **R** (red). The block changes that side only and passes the other side through, delayed by exactly as much as its own side, so the two ears stay in step. Changing the Ear while the session runs fades across instead of clicking, and one undo puts it back. Loading a preset or putting the block back to its default settings never changes it, so a preset made for one ear can be tried on the other.

A new block starts on the left ear. When the session already has more blocks of the same kind for the left ear than for the right, a new one starts on the right instead, so adding the same block twice gives you one for each ear.

There are two ways to give each ear blocks of its own:

- **In series, on one lane:** a block set to Left, then the same kind of block set to Right. It's simple, but the delays of every block on the lane add up.
- **Side by side, on two lanes:** wire the source to two lanes, one for each ear, and bring them back together with **Join L/R**. Its **Left** input takes the left side of one lane and its **Right** input the right side of the other. Reverblabr lines the two lanes up where they meet, so the ears stay in step even when one lane has more blocks than the other, and the session is only as late as the slower lane.

To set up two lanes with Hearing Compensation 16 (any hearing block works the same way):

1. Select the source and click **Hearing Compensation 16** in the library. It's wired after the source and starts on the left ear.
2. Select the source again and click **Hearing Compensation 16** once more. The new block gets a lane of its own from the same source and starts on the right ear.
3. Select the left ear's block and click **Join L/R**. It takes that block on its **Left** input.
4. Hold Ctrl and drag from the right ear's block to the **Right** input of Join L/R.
5. Select Join L/R and add the output, such as an Output Device.

## Hearing Compensation 16

**Hearing Compensation 16** gives one ear more of what it hears faintly. In each of sixteen bands it has three gains, for **soft**, **average** and **loud** sound, and a **ceiling** that nothing in that band goes above. Soft sound gets the most lift and loud sound the least, so quiet speech comes up without loud sound becoming too much.

- **The Gain view** draws the three gains and the ceiling across the frequencies. Pick **Soft**, **Average**, **Loud** or **Ceiling** above it, then drag a band's handle up or down. The dark line is the gain each band is giving right now, with the sound playing drawn behind it.
- **The Levels view** shows where the sound sits for this ear: the quietest levels it hears (from the audiogram), the ceilings, and the sound going in and coming out.
- **Click a handle** to see that band's settings as numbers.
- **Speed** decides how quickly the gain follows the sound. **Syllabic** follows speech syllable by syllable, **Slow** keeps the shape of music, and **Adaptive** does both and catches a sudden loud sound at once.
- **Loudness** turns all bands up or down together. **Noise floor** keeps the lift from bringing up the hiss of a recording. **Most gain** is the most any band ever gets. **Output ceiling** is a limiter at the very end.
- **Hold for before** plays the sound without the block for as long as you hold the button, so you can compare.

With a block for each ear, each panel draws the other ear's curve faintly, and **Copy from the Other Ear** takes over its settings as a starting point.

## From an audiogram

If you have an audiogram (the chart from a hearing test), you don't have to set the bands by hand. Open **Audiogram**, enter the quietest level heard at each frequency, and, if you know them, the levels where sound becomes too loud. Then press **Fit from Audiogram**. All bands are set in one step, and one undo takes it back.

A band where more gain is unlikely to help is marked. Often a better idea there is to move that sound somewhere the ear hears it, with [Frequency Lowering](#frequency-lowering).

The fit follows published fitting rules, not something I made up. [From an audiogram to band settings](fitting-rules.md) explains how it works and where the numbers come from. Treat the result as a starting point and adjust by ear.

## Hearing Compensation 64

**Hearing Compensation 64** works the same way with sixty-four bands, for a steep slope, a narrow dip or a sharp edge that sixteen bands can't follow. Instead of one handle per band you set **points**, and the bands follow smooth curves through them that never overshoot.

- **Drag a point** sideways to move it, and up or down to change the curve you picked above the view.
- **Double-click** an empty spot to add a point (up to 24). It's placed on the curves as they are, so nothing changes until you move it. **Remove Point** takes the selected one out.
- **Fit from Audiogram** places a point at each frequency the audiogram has a level for.

## Frequency Lowering

Some ears hear nothing in a high range however loud it is, and an important part of speech lives up there: the hiss of **s**, **sh** and **f**, which is what tells "sit" from "fit" and "cat" from "cats". **Frequency Lowering** moves that sound down into a range the ear does hear.

1. Add it for the ear that needs it and choose that ear under **Ear**. Add a second one for the other ear if that one needs it too: it starts on the right.
2. Its first move is ready to go: sound from **4 to 8 kHz** squeezed into **2.5 to 4 kHz**, with the high range emptied. On the **Arrows** view, the shaded range is where the sound comes from and the outlined one is where it goes. Drag the ends of either range to change them.
3. Choose how each move carries the sound:
   - **Squeeze** fits the whole range into a smaller one lower down. Use it where the ear hears nothing at all, and **Empty** the range it came from.
   - **Copy down** adds a copy an octave or so lower and keeps the original, either **As it was** or **Quieter**. Use it where the ear still hears some of the high range.
4. **Acts: On s, sh, f** moves sound only while the high range carries most of it, so vowels and music pass unchanged. **Always** moves everything in the range.
5. Turn on **Moved sound only** to hear just what the moves add while you set their **Level**, then turn it off again.

Up to four moves can work at once, each from its own range. The **Map** view shows each move as a line from the frequency that goes in to the one that comes out.

Put Frequency Lowering **before** any block that sets the level of each range, such as Hearing Compensation, so the moved sound gets the level of the range it lands in. It delays its ear by about 21 ms, and the other ear waits as long.

## Frequency Raising

Some ears hear the low range faintly or not at all, while the higher range is fine. **Frequency Raising** is Frequency Lowering the other way round: it moves sound from a low or middle range up into one the ear hears. The panel, the moves and the views are the same.

1. Add it for the ear that needs it and choose that ear under **Ear**.
2. Its first move is ready: sound from **250 to 500 Hz** copied two octaves up into **1 to 2 kHz**, with the original kept.
3. Choose how each move carries the sound:
   - **Copy up** adds a copy higher up and keeps the original.
   - **Squeeze** fits the whole range into another one higher up. Use it where the ear hears nothing at all in the low range, and **Empty** the range it came from.
   - **Add harmonics** is experimental. Moving a voice up raises its pitch. This leaves the voice where it is and adds its harmonics (twice its frequency, three times, and so on) wherever they fall in the destination. The ear takes the pitch from the harmonics, so the voice still sounds like itself. Not much is known about how well this helps, so trust your ears.
4. **Acts: On low sounds** moves sound only while the low range is louder than everything above it, such as a voice or a hum. **Always** moves everything in the range.

Frequency Raising needs a longer stretch of sound to tell low sounds apart, so it delays its ear by as much as its lowest move needs: about **43 ms** while every move starts at 250 Hz or above, and about **85 ms** once a move starts lower. A move switched off still counts, so switching it on and off to compare doesn't move the ear in time; take it out of the list to stop it counting. The panel's **Delay** says which it is. When a move crosses 250 Hz while the session runs, the lane fades out, stays quiet for about as long as the new delay, and fades back in: a short gap, not a click. Even 43 ms is enough to notice with video (lips move before you hear the voice), so it's better kept for listening.

### A dip in the middle

Some ears hear the lows and the highs but not a range in the middle. You can take the dip apart with both blocks, set to the same ear, one after the other:

1. **Frequency Lowering** moves the lower half of the dip down below it. For a dip from 1 to 4 kHz: sound from 1 to 2 kHz squeezed into 500 Hz to 1 kHz.
2. **Frequency Raising** moves the upper half up above it: sound from 2 to 4 kHz squeezed into 4 to 8 kHz.

With the halves split like this, the order doesn't matter. If the ranges overlap, the second block moves what the first one has already moved, so put first the move you want the other to work on. Both belong before any block that sets the level of each range.

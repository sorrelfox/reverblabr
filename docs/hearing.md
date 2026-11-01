<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Hearing

One of the things I want Reverblabr to do well is help people who hear part of the sound poorly: someone who can't hear the high frequencies any more, who has a dip somewhere in the middle, or whose two ears hear differently. The blocks in the **Hearing** group of the library are for that. They work on one ear at a time, so each ear can be set up for the way it hears, and they can work from an audiogram if you have one.

Please keep two things in mind:

- **Reverblabr is not a medical device.** It shapes sound for listening and measures nothing about anybody's health. It doesn't replace an audiologist or a hearing aid. If your hearing has changed, have it checked.
- **Be careful with loud sound.** These blocks can add a lot of gain. Start with the volume low, especially with headphones, raise it slowly, and leave the **Output ceiling** in place.

## Before you add a Hearing block

Reverblabr shows these words once, before the first block of the Hearing group is added. They stay in the **Help** menu.

**Keep it comfortable.** Sound should never be uncomfortable or painful. If it is, turn it down or take your headphones off. Start with the volume low and raise it slowly.

**These limits do not protect your ears.** They work on numbers inside the computer. How loud that is at your ear depends on your volume, your sound card and your headphones, which Reverblabr can't know. Long hours of loud sound can harm hearing with no warning.

**Some changes need a doctor, not a setting.** See a doctor straight away if your hearing changes suddenly or gets much worse within days, in one ear or both. Time matters. If this comes with a weak or numb face or a spinning feeling, or after a blow to the head, call for emergency help. See a doctor soon if one ear is worse than the other, your hearing comes and goes, you hear ringing in one ear or a beat in time with your pulse, you feel dizzy, or an ear hurts, feels blocked, looks injured or leaks blood, pus or fluid. Ringing that keeps you awake or gets you down is worth a visit too. Don't turn one side up to make up for a change: a setting can hide it.

**Before you add gain.** Don't add these blocks on top of headphones or earpieces that already shape the sound for you: the gains add up. That holds on loudspeakers too, if you wear such earpieces while you listen. Don't set them up for a child. Don't rely on a session for alarms or calls you must not miss: if it stops, the sound can stop or change. Don't wear closed headphones where you need to hear traffic.

## One ear at a time

Every block in the Hearing group except Join L/R and One Ear Mix has an **Ear** setting, Left or Right, at the top of its settings, and its tile shows an **L** (blue) or an **R** (red). The block changes that side only and passes the other side through, delayed by exactly as much as its own side, so the two ears stay in step. Changing the Ear while the session runs fades across instead of clicking, and one undo puts it back. Loading a preset or putting the block back to its default settings never changes it, so a preset made for one ear can be tried on the other.

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

- **The Gain view** draws the three gains and the ceiling across the frequencies. Pick **Soft**, **Average**, **Loud** or **Ceiling** above it, then drag a band's handle up or down. The dark line is the gain each band is giving right now, with the sound playing drawn behind it. The numbers round the graph give the frequency in Hz and the gain in dB; the sound behind has a scale of its own, which the line under the graph gives.
- **The Levels view** shows where the sound sits for this ear: the quietest levels it hears (from the audiogram), the ceilings, and the sound going in and coming out. The numbers are digital level, in dBFS. Levels typed in dB HL are drawn 85 dB lower, a fixed figure that does not measure your sound system. A ceiling above 0 dBFS holds nothing back and has no place on this scale, so the words on the graph say that instead.
- **Click a handle** to see that band's settings as numbers.
- **Speed** decides how quickly the gain follows the sound. **Syllabic** follows speech syllable by syllable, **Slow** keeps the shape of music, and **Adaptive** does both and catches a sudden loud sound at once.
- **Loudness** turns all bands up or down together. **Noise floor** keeps the lift from bringing up the hiss of a recording.
- **Most gain** is the most any band ever gets, and **Output ceiling** is a limiter at the very end. Both sit under **Limits**.
- **Hold for before** plays the sound without the block for as long as you hold the button, so you can compare.

With a block for each ear, each panel draws the other ear's curve faintly, and **Copy from the Other Ear** takes over its settings as a starting point. Its limits come too, and a copy that would raise one asks first.

**Limits.** Most gain and Output ceiling are digital limits. They work on numbers inside the computer. How loud that is at your ear depends on your volume, your sound card and your headphones, which Reverblabr can't know.

Raising a limit takes one more step, and the sound rises slowly to it. Lowering a limit takes effect at once. Taking Loudness past +10 dB in the band that has come up the most takes one more step too, with **Go Further...**.

If a block that can add gain comes after the last limit, the Output Device adds a limit of its own, so that nothing passes the lowest Output ceiling. It says which block caused it, and it adds about 2 ms of delay.

Commands and D-Bus meet the same ranges and no dialog: a script is a deliberate act.

## From an audiogram

If you have an audiogram (the chart from a hearing test), you don't have to set the bands by hand. Open **Audiogram**, enter the quietest level heard at each frequency, and, if you know them, the levels where sound becomes too loud. Then press **Fit from Audiogram**. All bands are set in one step, and one undo takes it back. No band gets more than **Most gain**, nor more than 50 dB however high Most gain is.

A band where more gain is unlikely to help is marked. Often a better idea there is to move that sound somewhere the ear hears it, with [Frequency Lowering](#frequency-lowering).

The fit follows published fitting rules, not something I made up. [From an audiogram to band settings](fitting-rules.md) explains how it works and where the numbers come from. Treat the result as a starting point and adjust by ear.

## Hearing Compensation 64

**Hearing Compensation 64** works the same way with sixty-four bands, for a steep slope, a narrow dip or a sharp edge that sixteen bands can't follow. Instead of one handle per band you set **points**, and the bands follow smooth curves through them that never overshoot.

- **Drag a point** sideways to move it, and up or down to change the curve you picked above the view.
- **Add Point** puts a point in the middle of the widest gap between the points, or **double-click** an empty spot to put one there (up to 24). It's placed on the curves as they are, so nothing changes until you move it. **Remove Point** takes the selected one out.
- **Fit from Audiogram** places a point at each frequency the audiogram has a level for.

## Frequency Lowering

Some ears hear nothing in a high range however loud it is, and an important part of speech lives up there: the hiss of **s**, **sh** and **f**, which is what tells "sit" from "fit" and "cat" from "cats". **Frequency Lowering** moves that sound down into a range the ear does hear.

1. Add it for the ear that needs it and choose that ear under **Ear**. Add a second one for the other ear if that one needs it too: it starts on the right.
2. Its first move is ready to go: sound from **4 to 8 kHz** squeezed into **2.5 to 4 kHz**, with the high range emptied. On the **Arrows** view, the shaded range is where the sound comes from and the outlined one is where it goes, with the frequencies in Hz along the bottom. Drag the ends of either range to change them.
3. Choose how each move carries the sound:
   - **Squeeze** fits the whole range into a smaller one lower down. Use it where the ear hears nothing at all, and **Empty** the range it came from.
   - **Copy down** adds a copy lower down and keeps the original, either **As it was** or **Quieter**. Every sound moves by the same step, from the bottom of one range to the bottom of the other: about two thirds of an octave with the ranges a new block starts with. Use it where the ear still hears some of the high range.
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

## One Ear Mix

**One Ear Mix** puts both sides of a stereo mix into the ear you choose, or one mix on both sides for a single earbud. Put it after anything that needs the two sides, just before the lane forks, with a limit after it. A new block changes nothing until you pick where the sound goes in **Send to**: Left ear, Right ear or Both ears.

**Mono** in the Channels block adds the sides and halves them, which drops sound panned to one side by 6 dB and cancels sound that is out of phase between the sides. One Ear Mix first shifts the two sides a quarter of a cycle apart, so panned sound drops 3 dB and that sound stays. A sound that sits a quarter of a cycle apart between the sides can get quieter instead. Peaks can rise by several dB, so keep a limit after it. The Volume block has one. Direction doesn't come back: a mix in one ear can't tell you where a sound is.

The sound on the side you chose goes on playing, and what was on the other side comes in over about five seconds. The side comes in the same way when the block is switched on again. The tile says which side has sound, in words. If you switch the block off, or the session stops, stereo comes back, and what is on the other side is lost to that ear again.

To check it against an ordinary mono mix, open **Compare with Plain Mono**. **Hear** switches between the two. **Match Levels** reads both for three seconds while something plays, and if One Ear Mix was the louder it fills in **Lower this mix by**, up to 6 dB, so the comparison is about the sound and not the level. Plain Mono is also the better choice for a mono recording in two channels.

## Sudden Sounds

**Sudden Sounds** softens sudden sharp sounds, such as clicks and knocks, without turning everything down. It watches five ranges and acts for moments at a time. A sound that stays loud is left alone after about a quarter of a second.

A new block changes nothing until you choose a **Strength**. Put it before any block that moves or lifts sound. Leave it off for music: it softens drums and plucked notes too. If you see it acting while nothing sudden happens, it is touching speech: **Gentle** takes less off each time, and **Off** stops it. A door's low thump goes through nearly as it came. A cut can leave a peak a little higher than it came in, even though it only ever takes sound away. Its thresholds are provisional: they have not yet been set on recordings of real sounds.

## Speech Now

**Speech Now** is a block under Meters that shows a word while a voice is on a lane, on its tile, in the tray and on a small label over other programs, and for a moment after it stops. It reads the sound and doesn't change it. It can be wrong, and it can't tell who is speaking. See [the block](blocks.md#speech-now).

## Voice Gauge

**Voice Gauge** is a block under Meters for a microphone lane. It shows the level of the voice over the last few seconds, holds it through pauses, and says where it stands against two marks you place, and nothing about how you sound. It reads the sound and doesn't change it. It can be wrong, and it reads whatever voice is there. It is a level before the call program, so it can't see a mute there or what the other side hears. See [the block](blocks.md#voice-gauge).


## Sound Side

**Sound Side** is a block under Meters that compares the two channels of a lane and shows the louder one, as a mark and a word, on its tile, on its card and in a thin strip over other programs. It reads the sound and doesn't change it. It says which channel is louder and nothing about what made a sound, and it can't tell front from back. A loud bed hides a quiet sound, and music and voices move the mark too, so it can be wrong. Put it before One Ear Mix, since after a fold there are no sides left. Sound Side reads this lane's sound and does not touch the game. Some games and tournaments still forbid tools that show direction. Check their rules before you use it. That choice is yours. See [the block](blocks.md#sound-side).
## Night Level

**Night Level** is a block under Built-in Effects for films and shows on one lane. It finds the level of speech and brings louder and quieter sounds part of the way towards it, by amounts you set apart. A new block is set to nothing: press Light, Night or Strong on its card. Until it hears speech it changes nothing, so the start of a film passes as it came. The level comes from a voice detector and it can be wrong, so you can type one. A sudden sound can still start loud, since the block can't know what comes next. Its limit is a digital one, not a level at your ears. It presses music too, so turn it off for music, and use it or Voice Evener on a lane, not both. Put it before One Ear Mix.

## Voice Forward

**Voice Forward** is a block under Built-in Effects for films and shows in stereo. It finds what is alike on both sides and turns the rest down with one control, Background, less between voices. It finds sameness and not voices, so a centred drum stays and a voice off to one side goes down. At 0 dB or switched off the sound passes unchanged, only about 23 ms later. On mono it leaves the voice band alone and moves only what lies under 100 Hz and over 12 kHz, and One Ear Mix belongs after it. Hold for before, on its card, plays the sound without it for as long as it is held. Don't use it in a game, where sounds at the sides tell you where things are. No measurement here shows that it helps anyone follow speech.

## Voice Evener

**Voice Evener** is a row in the library under Built-in Effects: an Auto Level set up to bring a quiet talker up and a loud one down towards one level, on one lane. It evens level only. A mixed call reaches a lane as one stream, so it is one gain for whoever is talking, and a new talker takes a few seconds to settle. Put it after Speech Denoise and before anything that shapes one ear. On music or a film it takes the loud and soft out: turn it off, or give them a lane of their own.

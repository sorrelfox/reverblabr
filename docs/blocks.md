<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Blocks

A session is made of blocks: sources that bring sound in, effects and meters in the middle, and outputs at the end. This page lists them in the order the library groups them. Every block has settings of its own, which open next to it on the canvas, and every block can save presets.

## Sources

| Block | What it does |
|---|---|
| **Application** | The sound of one program while it plays. It can take everything the program plays, only what it plays to the default output, or only what it plays to devices you choose. The program keeps playing to its own device as before. |
| **Input Device** | A microphone or any other recording device, or a [virtual output](virtual-devices.md) that other programs play into. *Default input* follows your desktop's choice. |
| **System Audio** | Everything the computer plays, minus the programs you leave out. Alert sounds are only included if you ask for them. |

## Outputs

| Block | What it does |
|---|---|
| **Output Device** | Where a lane is heard: the default output (followed when it changes), a device you choose, or a virtual microphone that other programs record from. It has a volume of its own. On a chain with a Hearing block where a block that can add gain follows the last limit, it adds a limit of its own and says why. |
| **Broadcast** | Sends what reaches it to an internet radio server (Icecast, or SHOUTcast) as MP3, Opus, Ogg Vorbis or AAC, and passes the sound on unchanged. Its **Setup** tab names the server, whether to reach it over TLS (Icecast only, usually on a port of its own such as 8443, with the server's certificate checked against the system's trusted authorities and the host's name, and no way to skip the check), the mount point, the user and the password, which is kept in your desktop's password store (KWallet on Plasma) and never in the session file; **Metadata** gives the station's name, genre, web page and description, and the title listeners see, made from a template such as `%artist - %title` and filled from a text file whose first line is the title, or from a media player: one you pick from those running, or by default the one the session's Application blocks capture (when they capture none, the one that is playing), its new title reaching the stream within a couple of seconds; **Status** shows how long it has been connected, whether the connection is encrypted, how much it has sent, the bit rate it is sending at and how many times it reconnected. When the server goes away it tries again, after a second and then longer each time, up to a minute. A wrong password, or a server certificate that is refused, stops it until you change something, and says why. Icecast 2.4 shows no title for an Opus stream on its own pages; listeners' players do. The block is built where QtKeychain is installed, which it keeps the password through. |
| **Live Stream** | Sends what reaches it to a video streaming service over RTMP or RTMPS, with a picture made for it, and passes the sound on unchanged. Its **Setup** tab takes the service's address (`rtmp://` or `rtmps://`) and the stream key, which is kept in your desktop's password store (KWallet on Plasma) and never in the session file, a quality (1024 × 576 at 15 frames a second, 1280 × 720 at 30, or 1920 × 1080 at 30) and the video encoder, the best one this computer has unless you choose; **Picture** gives the title (the session's name until you change it), a subtitle, what is playing, made from a template such as `%artist - %title` and filled from a text file or a media player as the Broadcast block's title is, a waveform, a spectrum or a level meter that moves with the sound, a background colour or picture, and a logo; the words are light or dark, whichever reads best over what is behind them; **Status** shows how long it has been on the air, how much it has sent and at what rate, the picture's size and encoder, and how many frames it sent and left out. The video is H.264, from the graphics card (VA-API or NVENC) where its driver can, and otherwise x264 or Cisco's OpenH264; the sound is AAC. When the service goes away it tries again, as Broadcast does. A computer with no H.264 encoder that works can't stream video, and the panel says so: on Fedora, install `openh264` (from Cisco's repository, on by default in Fedora Workstation), or the full FFmpeg from RPM Fusion. The block is built where QtKeychain, FreeType, HarfBuzz and fontconfig are installed. |
| **Recorder** | Writes what reaches it to a file and passes the sound on unchanged. See [Recording](recording.md). |
| **Wave** | A test tone (sine, square, triangle or sawtooth, 20 Hz to 20 kHz) or white or pink noise, at the level you set, on the left, the right or both. Wire it to an Output Device to check a speaker or a cable. |

## Built-in Effects

| Block | What it does |
|---|---|
| **Volume** | Turns a lane up or down, up to 400 % with overdrive on, with a limiter that keeps it from clipping. |
| **Dynamics Processor** | Holds loud sound back and pushes quiet sound down, along a curve you shape by dragging four points. The curve never goes above the dashed diagonal, and each point moves along one line only. A dot shows where the sound is on it. |
| **Simple Compressor** | Steadies the level with one choice (Gentle, Moderate, Heavy, Voice or Broadcast) instead of six controls, and shows what that choice sets. |
| **Multiband Compressor** | Three compressors side by side, for the lows, the middle and the highs. A loud bass doesn't pull the voice down with it, and a harsh "s" can be tamed on its own. Drag the lines between the bands to move where they meet. With every ratio at 1:1, the sound comes out as it went in. |
| **Limiter** | Keeps a lane under a ceiling between −12 and 0 dB, including peaks that fall between samples, and shows how hard it is working. |
| **Auto Level** | Brings quiet passages up towards a steady loudness and, if you ask, loud ones down, with a limiter so that the lift never clips. The **Voice Evener** row in the library adds it on the preset of that name, which does both, for voices on a call. |
| **10-Band EQ** | Ten sliders from 31 Hz to 16 kHz, ±12 dB each, with twenty presets, a preamp and an option to keep the overall level where it was. |
| **Bass & Treble** | Lifts or cuts the low and the high end by up to 12 dB, to warm a source up or brighten it. |
| **Low Pass Filter** | Rolls off hiss and other high noise above a cutoff, at 12 or 24 dB per octave. |
| **High Pass Filter** | Rolls off rumble, handling noise and the thump of a room below a cutoff between 20 Hz and 2 kHz, at 12 or 24 dB per octave. |
| **Echo** | Repeats the sound from a millisecond to two seconds later, each repeat quieter and duller than the one before, mixed in behind the original. |
| **Reverb** | Puts the sound in a room. See [below](#reverb). |
| **Pitch Shift** | Moves the pitch up or down by up to an octave in semitones, and a little further with cents, without making the sound faster or slower. See [below](#pitch-shift). |
| **Voice Compressor** | Holds a voice steady with three controls. Compression evens it out and makes up the level by itself, a gentle gate lowers the quiet between words, and a limiter stands at the ceiling you set. Delays the sound by about 2 ms. |
| **Night Level** | Finds the level of speech in what is playing, leaves sound near it alone, and brings louder and quieter sounds part of the way towards it. Until it hears speech it changes nothing. It presses music too, so turn it off for music. |
| **Voice Forward** | Finds what is alike on both sides of a stereo sound and turns the rest down with one control, less between voices. At 0 dB or switched off the sound passes unchanged. It can't tell a voice from centred music, and a voice off to one side goes down with the rest. About 23 ms of delay. |
| **Balance** | Turns the left or the right side of a stereo lane down against the other, all the way to silence. |
| **Pan** | Places a lane anywhere between left and right. The sound is folded to mono first and keeps the same loudness wherever you put it. |
| **Channels** | Swaps the sides, folds to mono, copies one side to both, mutes or inverts a side, or keeps only what differs between them, fading from one setting to the next. Its Mono drops sound panned to one side by 6 dB and cancels sound out of phase between the sides; **One Ear Mix**, in the Hearing group, folds them another way. |
| **Mono Enhancer** | Gives a mono recording some width above a frequency you choose, keeps the bass in the middle, and still sounds right when played back in mono. |

### Model

Beta: this block is still being debugged and may contain bugs. It is not yet fully tested.

Runs the sound through a trained model from a folder you choose, and plays what the model makes of it. The model runs in a process of its own, so a model that is slow or crashes cannot stop the session or the other lanes. Reverblabr includes no models and never downloads one. If the folder has a licence file, the card names it. The build needs ONNX Runtime; without it this block is not in the library.

**Sound only leaves this block after the model has processed it.** In every other case the block is silent, and nothing reaches the next block: when it is switched off (it is not passed through, unlike other blocks), with no model chosen, while one loads, when one cannot be loaded, when the model falls behind, when its process crashes, hangs or restarts, and when it gives back something that is not sound. A very short fade on the model's own sound keeps a stop from clicking. The input is never faded in. The session keeps running, the other lanes are not touched, and the card says why it is silent.

**Spare time** (20, 50, 100 or 250 ms) is the time a run may take, and is part of the delay, which is one hop of the model plus what its own processing needs plus this. It is the same at every quantum, and the other lanes are kept in step with it. The default of 50 ms covers quanta up to 2048 at 48 kHz.

**Mono models** run once a side, or once on the two sides mixed. A voice always runs once on the two sides mixed, and the card leaves this setting out for it.

A folder is recognised by its `reverblabr-model.json`, or by the inputs of its ONNX files. This version runs:

- a streaming DeepFilterNet export, known by its inputs `input_frame`, `states` and `atten_lim_db`, for noise cleanup at 48 kHz;
- an RVC-style voice conversion set of three ONNX files, described below;
- any ONNX model described by a `reverblabr-model.json`.

Speech through a language model and GGUF files are recognised and not run yet, and the card says so. Files that can run code when opened (`.pt`, `.pth`, `.ckpt`, `.pkl`) are never opened.

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
  "output": "out",
  "state": [{ "input": "state_in", "output": "state_out" }],
  "constants": { "strength": 1.0 },
  "threads": 1,
  "licence": "LICENSE.txt"
}
```

`file`, `sampleRate`, `channels`, `frameSize`, `input` and `output` are required. Inputs and outputs are 32-bit floats, of shape `[frames]`, `[channels, frames]` or `[1, channels, frames]`. A run takes `frameSize` samples and gives back `frameSize` or `hopSize`, of which the last `hopSize` are used. `latencySamples` is how late the model's answer is, at its own rate. Each `state` pair starts at zeros and carries a run's output to the next run's input. `constants` are inputs that hold one number. See [ADR-0031](adr/0031-model-block.md).

**A voice** is three ONNX files in one folder, told apart by their inputs and outputs and not by their names:

- the voice itself, an RVC-style export, known by its inputs `phone` or `feats`, `pitch` and `pitchf`;
- a content encoder, which hears what is being said, such as `vec-768-layer-12.onnx`. It has to give the size of features the voice takes, 256 or 768;
- a pitch estimator, which hears how high it is, such as `rmvpe.onnx`.

If one is missing or does not fit, the card says which. Voices share the other two, and a link to a shared file works. Use only voices you have the right to use.

**Pitch shift** moves the voice up or down, up to 24 semitones either way, and can be changed while it runs. A voice runs once on the two sides mixed, and both sides play it.

A voice takes far more than a cleanup model. Each run hears a second of sound and adds 160 ms, and its delay is about 390 ms with the default spare time, which is too long to hear yourself speak through it. The card's Delay is the true figure. An RVC v2 voice at 40 kHz took about two cores of a Ryzen 9 9950X3D2, and was done in 57 % of the time it had; a slower machine may need the Spare time raised, or may not keep up at all, and the card then says so.

A folder with more than one voice, or a voice whose rate its file does not show, needs a `reverblabr-model.json` to say which is which:

```json
{
  "format": 1,
  "family": "voiceConversion",
  "file": "voice.onnx",
  "contentModel": "encoder.onnx",
  "pitchModel": "estimator.onnx",
  "sampleRate": 40000,
  "threads": 4,
  "licence": "LICENSE.txt"
}
```

`sampleRate`, `threads` and `licence` may be left out.

### Reverb

Start from one of the presets in its settings (**Small room**, **Hall** or **Plate**), then adjust:

- **Decay** is how long the room rings after the sound stops, from a tenth of a second to twenty seconds.
- **Size** is how big the room is. Small rooms ring densely and close, large ones spread out.
- **Damping** makes the top end of the ring fade sooner, the way soft walls and air do.
- **Pre-delay** holds the room back for up to a quarter of a second, so you hear the sound clearly before the room joins in.
- **Width** spreads the room from the middle out to both sides, and **Mix** sets how loud it is against the original.

The original goes straight through, so a Reverb doesn't delay the lane.

### Pitch Shift

**Semitones** moves the pitch in whole steps, like the keys of a piano, up to an octave either way. **Cents** are hundredths of a semitone on top, for tuning one sound to another. The sound comes out about 60 ms late, and a new setting fades in over a few tens of milliseconds, so a change never clicks.

## Meters

| Block | What it does |
|---|---|
| **Peak/RMS** | A level meter for any point in a lane, with peak hold and clip lights. Its RMS bar has a paler bar above it that jumps up to each peak; untick **Show peak bar** in its settings to hide that one, on the tile and in the system tray. |
| **VU** | A pair of needles that follow the average level, the way a tape machine's meters did, against a reference level you choose. |
| **Speech Now** | Shows Speaking while a voice is on this lane, and Quiet when not. For a moment after a voice stops it shows Quiet again. It can't tell who is speaking or what was said, and the sound passes unchanged. |
| **Voice Gauge** | Shows the level of a voice at this point on the lane, in LUFS, against two marks you place. It says where the reading stands against your marks, and nothing about how you sound. It can be wrong, and the sound passes unchanged. |
| **Waveform** | Draws the sound at this point in a lane as it goes by: an oscilloscope, on its tile and on its card. The sound passes unchanged. |
| **Spectrum** | Shows how much of each frequency there is at this point in a lane, as a row of bars: a spectrum analyzer, on its tile and on its card. The sound passes unchanged. |
| **Sound Side** | Shows which channel is louder, with a moving mark and a word. It can't tell what made the sound, and it can't tell front from back. The sound passes unchanged. |

Any of the level meters can also be shown in the system tray, see [On the desktop](desktop.md#the-tray-icon).

### Speech Now

Put one right after the block whose sound you want to watch, before anything that changes the level. Name it in its card: a tile called Call and a tile called You read as two talkers. Nothing else tells two voices on one lane apart.

**Quiet again** says what the sound did. A pause for thought and the end of a turn look the same, so it does not mean a talker has finished. **Hold** is how long a voice may pause and still count as Speaking, and a longer one gives fewer false stops and shows the true stop later. The **Steady** preset is the longer one. **Shortest talk** keeps a cough from earning the mark. Music, singing and film can read as Speaking.

**Detector** picks how it listens. **Voice model** is a small model on this computer. It needs a sound system at 48 kHz and a build that has it, and the card says so when one is missing. **Level and rhythm** uses far less of the processor and can be wrong more often. It misses voices in a noisy room and passes music with a beat.

Under **Show it on**, **Floating label** adds a row to a small window that stays over other programs, up to four rows from any open session. It stays when you close the main window, while there is a system tray. Drag it to move it, or set its place under **Place the label**. Turn it off in the card or in the tray's popover. It reads **Not watching** whenever the block can't hear its lane: the session stopped, the session is overloaded, or the engine stopped answering. A full-screen share may carry the label, name included.

Nothing is spoken unasked. With a screen reader, set a key for **Say Who Is Speaking** ([global shortcuts](desktop.md#global-shortcuts)).

### Voice Gauge

Put it right after the microphone's Input Device and name it You. With nothing wired after it, the call program keeps the real microphone, and a stopped session costs the call nothing.

The number is the level over the last few seconds of voice, in LUFS. It takes a second of voice to appear. In a pause it stays and the word says **Held**, and after **Forget after** with no voice it starts clean. It moves with the voice, the distance to the microphone and the gain, and the block can't tell them apart. It is a level at this point, before the call program. It can't see a mute inside that program or what the other side hears, and a voice from a loudspeaker counts too.

**Upper mark** and **Lower mark** start Not set, as a built-in level would say what a voice should be. After 20 seconds of voice, **Use This Level** places them around that level, **Room each side** apart from it, and the card notes the microphone they were placed on. If someone is on a call with you, ask them to say when the level suits them and press it then. Marks hold for this microphone at this gain. You can also type them. Moving one past the other pushes it along. The words are **Between marks**, **Above upper mark** and **Below lower mark**, or with one mark **Above** or **Below** it, and a word changes only half a unit past a mark. The bar is one colour wherever it stands, and no sound or notice comes at a mark.

**No sound here** means nothing has reached this point for 2 seconds. It can't say why. To check, say a word and watch the dot: orange means a voice is being counted, and says nothing further along.

**Reading over** is how many seconds of voice the number is the mean of. The **Quick** and **Steady** presets set it, and leave the marks alone. **Detector** is the same as in Speech Now.

Put it before Voice Evener. After that block the reading sits near its target and hides what the voice did. After Speech Denoise, a pause can read as No sound here. A Bluetooth headset drops to hands-free sound while anything records from it, and a session left running holds it there outside calls too.

Under **Show it on**, **Floating bar** adds a row to the same small window as Speech Now's label, with the number, the word and a track. On the track the marks keep one place, a third and two thirds along. Set a key for **Say Voice Level** to have your screen reader say the number and the word ([global shortcuts](desktop.md#global-shortcuts)). Nothing is spoken unasked.

### Waveform

Put one before and one after a block to see what it does to the shape of the sound. It passes the sound on with no delay, and it works only while its tile or its card is on the screen. A session can draw up to 16 Waveform and Spectrum blocks at once. A card outside the main window goes on drawing.

**Style** draws a **Line**, the wave itself, or the band it fills when more than one sample falls on a pixel. **Filled** is how far the sound reaches either side of the middle as a solid shape, **Bars** the same in separate bars, and **Dots** marks the highest and lowest points. **Time shown** goes from 10 ms, the shape of a wave, to 10 s, how the level moves, and a single click stays visible even at 10 s.

**Refresh rate** is how often a new look is taken at the sound, from 10 to 120 times a second. The sound does not come through the 60-a-second levels, so a rate above 60 is real. It can't show more than arrives, one cycle of the sound system at a time, and the card says so when you ask for more. **Motion** decides what happens between two looks. **Real-time** shows the newest sound as it is and holds it. **Smooth** draws at the rate of your screen: from 200 ms up the trace flows steadily across, a little behind the newest sound, and below that, or with the trigger on, each new trace eases in from the last, lined up on a rising zero crossing. Smooth is calmer and uses more of the graphics card.

**Gain** enlarges the drawing and not the sound, and **Auto fit** does it for you: at once when the trace would go off the edge, and over about a second when it gets quieter. **Channels** picks **Both** over each other, **Left**, **Right**, **Mid**, **Side**, or **Stacked** with left above right. **Scale** on **Decibels** spreads the last 60 dB over the height, so that quiet sound can be seen.

**Start on a rising zero crossing** holds a steady tone still. On speech or music there are many crossings and the trace still moves, and with none to find, the card says so and the newest sound is shown. **Hold** keeps a moment on the screen, and you can change how it is drawn while it is held. It is kept for the session but left out of presets, and a trace that was held is gone once neither the tile nor the card is on the screen.

A trace that stands still is one you asked to hold. When the session is stopped or overloaded, the block is off, or nothing is arriving, no trace is shown and a line of words says why.

The presets are **Speaking**, **Single tone**, **Long view** and **Left and right**.

### Spectrum

Put one before and one after an equalizer or a filter to see what it does, or use one to find the frequency of a hum or a whistle. It passes the sound on with no delay, and it works only while its tile or its card is on the screen. A session can draw up to 16 Waveform and Spectrum blocks at once. A card outside the main window goes on drawing.

A bar stands at the loudest thing in its range of frequencies, with 0 dBFS at the top, so a sine wave at full scale reaches the top in the bar that holds it, to within 0.4 dB, whichever settings you use. The range goes from 20 Hz to 20 kHz, or to half the sample rate if that is lower. It is worked out from every sample, so the top of the range is as true as the bottom.

**Bars** divides the range into 16 to 128, and a tile shows fewer when it has no room for them all. **Frequency scale** gives each octave the same width on **Logarithmic**, and each hertz the same width on **Linear**, which suits looking at harmonics.

**FFT size** is how much sound each look takes in, from 1024 to 8192 samples, and the card says what that comes to. A large size tells low notes apart but blurs quick sounds together, and a small size does the opposite. At the bottom of a logarithmic scale several bars may share what one size can tell apart. They all show it, so a low sine draws a hump as wide as the size makes it, and no bar is left empty.

**Refresh rate** is how often the bars are worked out, from 10 to 120 times a second. A slow rate misses nothing: the bars show the loudest moment since the last refresh, as far back as the block keeps sound, which is 8191 samples or 170 ms at 48 kHz. It can't show more than arrives, one cycle of the sound system at a time, and the card says so when you ask for more. **Motion** decides what happens between two refreshes. **Real-time** shows each refresh's bars as they are and holds them. **Smooth** moves the bars at the rate of your screen: they rise quickly and ease down when the sound falls. Smooth is calmer and uses more of the graphics card.

**Level range** is the level at the bottom of the view, from -60 to -120 dBFS. The top is always 0 dBFS. **Tilt** lifts high frequencies and lowers low ones by 3 or 4.5 dB an octave, turning about 1 kHz, in the drawing and not in the sound. Music is usually strongest at the bottom, and with tilt it is easier to compare across the range. With tilt on, a bar's height is no longer the true level.

**Channels** picks **Mix**, **Left** or **Right**. Mix is the two added and halved, as a mono mix would be: a sound in one channel reads 6 dB lower, and sound in opposite phase cancels out. **Style** draws **Solid** or **Segmented** bars. **Peak marks** puts a mark over each bar at its highest, which stays for a second and then falls slowly. **Grid and labels** draws lines behind the bars at round frequencies and every 20 dB, and the card names them.

**Hold** keeps the bars on the screen, and you can change how they are read while they are held. It is kept for the session but left out of presets, and held bars are gone once neither the tile nor the card is on the screen.

Bars that stand still are bars you asked to hold. When the session is stopped or overloaded, the block is off, or nothing is arriving, no bars are shown and a line of words says why.

The presets are **Music balance**, **Fine detail**, **Quick look** and **LED bars**.

### Sound Side

Put it after an Application block that holds only the game and before One Ear Mix. After a fold there is nothing left to compare, and the card offers **Move Before One Ear Mix**. Turn off the game's own mono option and use One Ear Mix after this block instead.

The word is **Left**, **Right**, **Centre** for near-equal sides that match, **Both** for near-equal sides that do not, **Quiet** under **Level gate**, or **Same** when both sides have carried one sound for 5 seconds. A side needs a quarter of the way, about 2 dB between the channels, and a word has to stand for a tenth of a second. The mark does not wait: it jumps at once, stands for a quarter of a second, and fades over another. With no sound there is no mark, so silence never reads as a sound straight ahead.

It compares energy on two channels and nothing else. A loud bed hides a quiet sound: a sound on one side alone leaves Centre only within about 2 dB of one channel of the bed. **Band** on **Range** compares only what lies between **From** and **To**. The **Whole mix** and **Higher sounds** presets set it. To stays an octave above From.

**Strip on screen** shows a thin dark strip over other programs, the same drawing as on the card. It takes no clicks and no focus, and it touches nothing of the game's. **Place Strip** gives it a frame and the keyboard until you press Enter or Esc, **Test Strip** runs the mark across once on the strip alone, and **Show** chooses a **Mark** or two **Bars**. Size, Show and the place belong to this desktop. There is one strip, and with several blocks the first has it. It goes with the window and the tray icon. Set a key for **Show or hide Sound Side's strip** ([global shortcuts](desktop.md#global-shortcuts)), which runs the test once each time it shows the strip. Direction is never spoken: the card's **Side now** is there to land on.

Sound Side reads this lane's sound and does not touch the game. Some games and tournaments still forbid tools that show direction. Check their rules before you use it. That choice is yours.

## Advanced

| Block | What it does |
|---|---|
| **Parametric EQ** | Up to ten bands you place yourself (bells, shelves, pass filters, notches), dragged along the curve they draw, with the live spectrum behind it. |
| **Ducking** | Turns a background source down while the main one (usually a voice) is heard, and back up when it stops. Its tile has two inputs: main and background. |
| **Mixer** | Five inputs on five strips with volume up to 200 %, mute, solo, balance and timed fades, under one master, plus five scenes to save and recall a set of levels. |
| **Input Switch** | Passes one of two inputs, A or B, and crossfades to the other at a click: half a second, or two seconds with Shift. |
| **Sync** | Holds a lane back by an exact number of milliseconds, up to two seconds, to line its sound up with a picture or with another lane. |
| **Time Shift** | Pauses and rewinds sound that is still arriving, such as a radio stream or a call. Jump back 3, 10 or 30 seconds, forward again, or straight back to live. It keeps up to an hour: up to ten minutes in memory, a longer history on disk. |
| **Declick** | Takes out clicks, pops and crackle (from a record, a bad cable or a digital dropout) and fills in what was underneath from the sound on either side. |
| **Denoise** | Listens to a few seconds of a steady noise on its own (a fan, hiss, an air conditioner), then removes that noise from everything that follows. The noise it learned is saved with the session. |
| **Speech Denoise** | Clears the background from a voice without learning anything first, using RNNoise, a model trained on speech. It works at 48 kHz and adds 30 ms of delay. It is only built where the distribution packages RNNoise, which Fedora does. |
| **Dehum** | Removes the 50 or 60 Hz hum of the mains and the whine of its harmonics, and leaves everything in between alone. With **Auto** on, it follows a hum whose frequency drifts. |
| **FIR Filter** | Applies an impulse response from a file: the sound of a room, a speaker cabinet, or a correction for a pair of headphones. Mono, stereo or true stereo, resampled to the session's rate, with about 5 ms of delay. |

## Hearing

These blocks work on one ear at a time. They are described on their own page, [Hearing](hearing.md).

| Block | What it does |
|---|---|
| **Hearing Compensation 16** | Lifts soft sound for one ear in sixteen bands, a lot for soft sound and little for loud sound, under a ceiling nothing goes above. It can be set band by band or from an audiogram. |
| **Hearing Compensation 64** | The same in sixty-four bands, set with points on smooth curves, for steep slopes and narrow dips. |
| **Frequency Lowering** | Moves sound from a high range the ear doesn't hear into a lower one it does, so the hiss of s, sh and f comes back, with about 21 ms of delay. |
| **Frequency Raising** | The other way round: moves sound from a low or middle range up into one the ear hears, with about 43 ms of delay, or 85 ms while a move starts below 250 Hz. Experimental in parts. |
| **Join L/R** | Puts two lanes back together as one stereo pair, the left side of one and the right side of the other, lined up in time, so that each ear can have its own blocks. |
| **One Ear Mix** | Puts both sides into one ear, or one mix on both sides for one earbud. Sound panned to one side comes through 3 dB lower, not 6, and sound out of phase between the sides doesn't cancel the way it does in **Mono**. A sound that sits a quarter of a cycle apart between the sides can get quieter instead, and peaks can rise by several dB, so keep a limit after it. |
| **Sudden Sounds** | Softens sudden sharp sounds, such as clicks and knocks, for moments at a time, one range of the sound at a time, for one ear. A sound that stays loud is left alone. It changes nothing until you choose a Strength. Leave it off for music. Less than a millisecond of delay. |

## Plugins

Reverblabr can run plugins that are already installed on your computer: **LV2**, **CLAP** (where the build has the CLAP SDK), **VST3®** (where the build has the VST3 SDK 3.8 or later) and **LADSPA**. The library lists what it found under **Plugins**, a heading for each format with the plugins under it in alphabetical order; click a heading to fold it away. The search looks through all of them. **Look for plugins again** looks again after you install something new. [What to install](dependencies.md#plugins) says where each kind is looked for. LSP's plugins are a good first set to try.

Every plugin runs in a separate process, so a plugin that crashes or hangs takes nothing else down with it. Its block is bypassed (the Model block is not a plugin and is silent instead), the sound goes on without it, and the block shows a button to restart it. A plugin's settings appear in a panel built from the parameters it offers.

An LV2, CLAP or VST3 plugin that has a window of its own shows it with **Plugin Window**, and the panel and the window follow each other. A knob turned in the window is one step for **Undo**, however long it was held. The plugin's own presets are listed in the panel under **Plugin presets**: an LV2 plugin's are the ones other LV2 programs see, and a VST3 plugin's are its `.vstpreset` files in `~/.vst3/presets` and the system's preset folders. **Save…** keeps the settings as one of them, where other programs that use the plugin find it too.

VST is a registered trademark of Steinberg Media Technologies GmbH.

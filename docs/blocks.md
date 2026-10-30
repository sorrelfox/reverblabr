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
| **Output Device** | Where a lane is heard: the default output (followed when it changes), a device you choose, or a virtual microphone that other programs record from. It has a volume of its own. |
| **Broadcast** | Sends what reaches it to an internet radio server (Icecast, or SHOUTcast) as MP3, Opus, Ogg Vorbis or AAC, and passes the sound on unchanged. Its **Setup** tab names the server, whether to reach it over TLS (Icecast only, usually on a port of its own such as 8443, with the server's certificate checked against the system's trusted authorities and the host's name, and no way to skip the check), the mount point, the user and the password, which is kept in your desktop's password store (KWallet on Plasma) and never in the session file; **Metadata** gives the station's name, genre, web page and description, and the title listeners see, made from a template such as `%artist - %title` and filled from a text file whose first line is the title, or from a media player: one you pick from those running, or by default the one the session's Application blocks capture (when they capture none, the one that is playing), its new title reaching the stream within a couple of seconds; **Status** shows how long it has been connected, whether the connection is encrypted, how much it has sent, the bit rate it is sending at and how many times it reconnected. When the server goes away it tries again, after a second and then longer each time, up to a minute. A wrong password, or a server certificate that is refused, stops it until you change something, and says why. Icecast 2.4 shows no title for an Opus stream on its own pages; listeners' players do. The block is built where QtKeychain is installed, which it keeps the password through. |
| **Live Stream** | Sends what reaches it to a video streaming service over RTMP or RTMPS, with a picture made for it, and passes the sound on unchanged. Its **Setup** tab takes the service's address (`rtmp://` or `rtmps://`) and the stream key, which is kept in your desktop's password store (KWallet on Plasma) and never in the session file, a quality (1024 × 576 at 15 frames a second, 1280 × 720 at 30, or 1920 × 1080 at 30) and the video encoder, the best one this computer has unless you choose; **Picture** gives the title (the session's name until you change it), a subtitle, what is playing, made from a template such as `%artist - %title` and filled from a text file or a media player as the Broadcast block's title is, a waveform, a spectrum or a level meter that moves with the sound, a background colour or picture, and a logo; the words are light or dark, whichever reads best over what is behind them; **Status** shows how long it has been on the air, how much it has sent and at what rate, the picture's size and encoder, and how many frames it sent and left out. The video is H.264, from the graphics card (VA-API or NVENC) where its driver can, and otherwise x264 or Cisco's OpenH264; the sound is AAC. When the service goes away it tries again, as Broadcast does. A computer with no H.264 encoder that works can't stream video, and the panel says so: on Fedora, install `openh264` (from Cisco's repository, on by default in Fedora Workstation), or the full FFmpeg from RPM Fusion. The block is built where QtKeychain, FreeType, HarfBuzz and fontconfig are installed. |
| **Recorder** | Writes what reaches it to a file and passes the sound on unchanged. See [Recording](recording.md). |
| **Wave** | A test tone (sine, square, triangle or sawtooth, 20 Hz to 20 kHz) or white or pink noise, at the level you set, on the left, the right or both. Wire it to an Output Device to check a speaker or a cable. |

## Built-in Effects

| Block | What it does |
|---|---|
| **Volume** | Turns a lane up or down, up to 400 % with overdrive on, with a limiter that keeps it from clipping. |
| **Dynamics Processor** | A compressor for loud sound and an expander for quiet sound, drawn as a curve you edit directly, with a dot showing where the sound is on it. |
| **Simple Compressor** | Steadies the level with one choice (Gentle, Moderate, Heavy, Voice or Broadcast) instead of six controls, and shows what that choice sets. |
| **Multiband Compressor** | Three compressors side by side, for the lows, the middle and the highs. A loud bass doesn't pull the voice down with it, and a harsh "s" can be tamed on its own. Drag the lines between the bands to move where they meet. With every ratio at 1:1, the sound comes out as it went in. |
| **Limiter** | Keeps a lane under a ceiling between −12 and 0 dB, including peaks that fall between samples, and shows how hard it is working. |
| **Auto Level** | Brings quiet passages up towards a steady loudness and leaves loud ones alone, with a limiter so the lift never clips. |
| **10-Band EQ** | Ten sliders from 31 Hz to 16 kHz, ±12 dB each, with twenty presets, a preamp and an option to keep the overall level where it was. |
| **Bass & Treble** | Lifts or cuts the low and the high end by up to 12 dB, to warm a source up or brighten it. |
| **Low Pass Filter** | Rolls off hiss and other high noise above a cutoff, at 12 or 24 dB per octave. |
| **High Pass Filter** | Rolls off rumble, handling noise and the thump of a room below a cutoff between 20 Hz and 2 kHz, at 12 or 24 dB per octave. |
| **Echo** | Repeats the sound from a millisecond to two seconds later, each repeat quieter and duller than the one before, mixed in behind the original. |
| **Reverb** | Puts the sound in a room. See [below](#reverb). |
| **Pitch Shift** | Moves the pitch up or down by up to an octave in semitones, and a little further with cents, without making the sound faster or slower. See [below](#pitch-shift). |
| **Balance** | Turns the left or the right side of a stereo lane down against the other, all the way to silence. |
| **Pan** | Places a lane anywhere between left and right. The sound is folded to mono first and keeps the same loudness wherever you put it. |
| **Channels** | Swaps the sides, folds to mono, copies one side to both, mutes or inverts a side, or keeps only what differs between them, fading from one setting to the next. |
| **Mono Enhancer** | Gives a mono recording some width above a frequency you choose, keeps the bass in the middle, and still sounds right when played back in mono. |

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

Either meter can also be shown in the system tray, see [On the desktop](desktop.md#the-tray-icon).

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

## Plugins

Reverblabr can run plugins that are already installed on your computer: **LV2**, **CLAP** (where the build has the CLAP SDK), **VST3®** (where the build has the VST3 SDK 3.8 or later) and **LADSPA**. The library lists what it found under **Plugins**, a heading for each format with the plugins under it in alphabetical order; click a heading to fold it away. The search looks through all of them. **Look for plugins again** looks again after you install something new. [What to install](dependencies.md#plugins) says where each kind is looked for. LSP's plugins are a good first set to try.

Every plugin runs in a separate process, so a plugin that crashes or hangs takes nothing else down with it. Its block is bypassed, the sound goes on without it, and the block shows a button to restart it. A plugin's settings appear in a panel built from the parameters it offers.

An LV2, CLAP or VST3 plugin that has a window of its own shows it with **Plugin Window**, and the panel and the window follow each other. A knob turned in the window is one step for **Undo**, however long it was held. The plugin's own presets are listed in the panel under **Plugin presets**: an LV2 plugin's are the ones other LV2 programs see, and a VST3 plugin's are its `.vstpreset` files in `~/.vst3/presets` and the system's preset folders. **Save…** keeps the settings as one of them, where other programs that use the plugin find it too.

VST is a registered trademark of Steinberg Media Technologies GmbH.

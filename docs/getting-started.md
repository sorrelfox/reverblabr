<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Getting started

This page walks you through a first session: the sound of one application, measured, turned up or down, compressed, measured again and sent to your speakers. It takes about five minutes. If you haven't built Reverblabr yet, start with [Installing](installing.md).

## The window

Start Reverblabr from a Plasma Wayland session. It opens with one empty session called **Untitled**.

The window has four parts:

- **The top row** holds your sessions, one tab each, with a **+** for a new one. On the right it shows what the session in front is doing, how long it has been running, and the **Run** button. This row is also the window's title bar: drag its empty part to move the window, double-click it to maximise. At the right end are the dark/light switch, the **☰** button with all the menus, the **Virtual Devices** button and the usual window buttons. **Ctrl+M** shows the menus as a menu bar instead.
- **The library** on the left lists the blocks you can add, grouped by what they do.
- **The canvas** in the middle is where a session's blocks and the wires between them live.
- **The status bar** at the bottom shows the rate and buffer size the audio system runs at, how busy the session is, and how many times it missed its deadline (an "xrun", heard as a dropout).

If you'd rather have Reverblabr look like the rest of your desktop, choose **Settings → Appearance → Follow System**. The window then takes your colours, fonts and title bar from Plasma, starting the next time Reverblabr opens.

## Build your first session

Start something playing first: a video in your browser, or music in any player.

Now add the blocks one after another. Clicking a block in the library adds it after the selected block, and with automatic connections on (they are, to begin with) wires it up as well:

1. **Application**, under *Sources*
2. **Peak/RMS**, under *Meters*
3. **Volume**, under *Built-in Effects*
4. **Dynamics Processor**, under *Built-in Effects*
5. **Peak/RMS** again
6. **Output Device**, under *Outputs*

That's one lane of six blocks, wired from left to right. The canvas scrolls to keep each new block in view, and **View → Fit to Window** (Ctrl+9) shows the whole session at once.

## Choose the application

The Application block doesn't know which program to listen to yet. Its tile says **Choose an app**.

1. Double-click the tile, or right-click it and choose **Settings**. Its settings open next to it.
2. Click **Choose an app** and pick your browser or player. The list has the programs that are playing now and the ones that played recently, and you can type the name of one that isn't running yet.
3. Under **Audio device selection**, leave *Capture audio played to the default output device*. That's what most people mean by "the sound of an application".
4. Press Escape, or click an empty part of the canvas, to close the settings.

The Output Device block starts on **Default output**, which is whatever your desktop plays to, and it follows along if you change that.

## Run it

Press **Run**, or Space. The button turns orange and says **Stop**, and the clock starts. The two meters show the level before and after the processing, the Dynamics tile draws its curve with a dot where the sound is on it, and the wires turn orange while sound goes through them.

You'll notice that you now hear the application twice. Reverblabr takes a copy of an application's sound and leaves the original alone, so the program goes on playing as it always did. To hear only the processed sound:

1. In the Application block's settings, under **Audio device selection**, choose *Capture audio played to all output devices*.
2. In the Plasma audio applet, move the application to an output you aren't listening to, such as an HDMI output with nothing plugged in.
3. Leave the session's Output Device on your speakers.

Don't turn down the application's own volume instead. PipeWire applies that volume before Reverblabr gets the sound, so the copy would go quiet too.

A cleaner way to do this, especially for several programs at once, is a [virtual device](virtual-devices.md).

## Change things while it runs

Everything can be changed while the session runs, without clicks or gaps:

- **Volume:** drag the slider on its tile, or open its settings to type a number. 200 % is twice the level, and overdrive takes it further.
- **Dynamics Processor:** open its settings and drag the handles on the curve. The upper one is where compression starts, the lower one where the expander starts. The dot shows where the sound is on the curve while you draw it, and the meter after the block shows what your change did.
- **Presets:** the row at the bottom of a block's settings. Some blocks come with presets, and **⋮ → Save as Preset…** keeps your own.
- **Undo:** Ctrl+Z takes back any change, including moving a block or a wire. The **Edit** menu tells you which change it would undo.
- **Moving a block:** drag it by its title. The rest of the tile is for its controls, like the Volume slider.
- **Keeping settings in view:** the window button at the top of a block's settings pulls them off the block into a window of their own, which stays where you put it while you work on other blocks. The pin button puts it back.

Press **Stop** when you're done. Sessions are saved as you work, so this one will be there the next time you start Reverblabr. It comes back stopped: a session never starts making sound just because Reverblabr was opened.

## Wiring blocks together

The switch at the top left of the canvas says how a session is wired. **Session → Automatic Connections** is the same switch.

- With **automatic connections**, a block you add by clicking is wired after the selected one, and taking a block out of the middle of a lane joins the lane up again behind it. It's the quick way to build a lane.
- With **manual connections**, nothing is wired for you.

Either way, you can wire blocks by hand: **hold Ctrl and drag** from a block's output connector on its right edge to another block's input connector on its left edge. The connectors that would accept the wire light up while you drag. Several wires can go into one input, and they are mixed there; a ring around the connector shows that. To remove a wire, click it and press Delete.

A block dragged from the library onto a spot on the canvas lands where you dropped it and isn't wired to anything, whatever the switch says. You picked the place, so Reverblabr doesn't guess.

## What the colours mean

The colours follow your Plasma colour scheme, so they work in light and dark themes. Orange is the exception: it's Reverblabr's own colour and always means that sound is going through.

- **Wires** are orange and moving while sound goes through them, and grey when nothing does (the session is stopped, or the lane is silent).
- **The dot** in a tile's top right corner is orange when the block is on and sound goes through it, green when it's on but nothing goes through at the moment, and grey when the block is off. The switch at the top of a block's settings turns it on and off.
- **Connectors**, the rings on the edges of a tile, are orange when their block has sound going through it.
- **Meters** show the level in decibels and say **Idle** when no sound reaches them. A meter that clips lights up and stays lit until you click it.

A block that is off is drawn faded. In the middle of a lane the sound simply goes past it, as if it weren't there. At the start or the end of a lane there's nothing to go past, so it's silent instead: a source that is off produces nothing, and an Output Device that is off sends silence to its device. A source keeps listening while it's off, so switching it back on is instant.

## Other sources

Besides Application, there are two other sources:

- **Input Device** takes a microphone or any other recording device, or what programs play into a virtual output. *Default input* follows whatever your desktop records from.
- **System Audio** takes everything the computer plays, in one block. You can leave programs out in its settings. Alert sounds are left out unless you ask for them.

Several sources can feed one lane. Wire them all to the same block and they're mixed there.

## Without a mouse

Everything on this page can be done from the keyboard, and the canvas works with a screen reader.

- **Add a block:** Ctrl+F jumps to the library's search. Type part of a name and press Enter for the first match, or walk the list with the arrow keys.
- **Move between blocks:** Tab and Shift+Tab go from block to block. A screen reader says each block's name, whether it's on and what it's wired to.
- **Wire two blocks:** on a block, press **C**. The wire aims at the first input that would take it, the arrow keys move it to the next one, **Enter** connects and Escape cancels.
- **Remove a wire:** on a block, press **D**, pick the wire with the arrow keys and press **Enter** or **Delete**.
- **Settings:** Enter on a block opens its settings, Escape closes them.
- **Run and stop:** Space.
- **The session as a list:** **View → Show as List** replaces the canvas with a list of the blocks, lane by lane, which is often easier to follow with a screen reader. The same keys work there, and the choice is remembered.

Meters are read out as progress bars named after their block, and a clipping meter is announced once, not on every hit.

In the Reverblabr appearance, text and every mark that means something (a state dot, a wire, a meter, the keyboard focus) meet the contrast levels of WCAG 2.1 AA. With **Follow System**, you get your colour scheme's own colours as they are.

## Where next

- [Blocks](blocks.md) describes every block in the library.
- [Virtual devices](virtual-devices.md) shows how to route programs, or your microphone, through a session.
- [Hearing](hearing.md) is about setting up the sound for each ear.
- [On the desktop](desktop.md) covers the tray, shortcuts and sessions that run at login.

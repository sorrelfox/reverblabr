<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# The Orca walkthrough

What a person does, by hand, to see that Reverblabr can be used with the Orca screen reader and a keyboard. The tests check what a screen reader is *told* (every control has a name, meters are progress bars, a clip is announced once); only a person with Orca running can check what it *says*, and whether that makes sense in order.

Run it on a Plasma session with Orca installed (`orca` on Fedora and Kubuntu). Leave the mouse alone from step 2 on. Note every place where Orca says nothing, says something wrong, or where a step cannot be done from the keyboard, and fix it or file it.

## Before you start

1. Start Orca (Super+Alt+S, or `orca` in a terminal) and check that it reads a Plasma menu.
2. Start Reverblabr with an empty session: **Session → New Session** (Ctrl+N).

## Build a session

3. **Ctrl+F.** Orca says the search field, "Search blocks".
4. Type `input`, press **Enter**. An Input Device block is added. Press **Ctrl+F**, type `volume`, **Enter**; then `output`, **Enter**. With automatic connections on, the three are wired in a row.
5. Press **Tab** until the focus reaches a block on the canvas, then go on through the blocks. Orca says each one's name and state ("Volume, On"), what it does, and what it is wired to ("Fed by Input Device, feeding Output Device").
6. On Volume, press **D**. Orca says "Wire Input Device to Volume, 1 of 2". **Down**: the other wire. **Esc**: the wires are as they were, and the focus is back on Volume.
7. On Volume, press **D**, then **Delete**. Orca says "Disconnected …". Press **Ctrl+Z**: the wire is back.
8. Switch automatic connections off (**Session → Automatic Connections**). Add a Reverb from the search as in step 4; it is not wired.
9. Tab to the Volume block, press **C**. Orca says "Connect to …, 1 of N". Use **Right** until it says Reverb, press **Enter**: "Connected Volume to Reverb".
10. On an Output Device block, press **C**. Orca says "Nothing can be connected to from Output Device".

## Set parameters

11. Tab to Volume, press **Enter**. Its settings open beside it. **Tab** through them: every slider, switch, box and button has a name Orca reads, saying what it sets (not only "slider, 50 %").
12. Change the volume with the arrow keys; Orca reads each new value.
13. **Esc** closes the panel. Note where the focus goes: it should be back on the block.
14. Repeat 11–13 on two or three other blocks with more controls (Reverb, Hearing Compensation 16, Pitch Shift).

## Run

15. Choose the Input Device's device: Enter on it, Tab to the device list, pick one with the arrows.
16. Press **Space** to run. Tab to a meter (a Peak/RMS meter block, added as in step 4). Orca reads it as a progress bar with a level in decibels, and does not chatter while it moves.
17. Make something clip (speak loudly into the microphone with Volume at +20 dB). Orca says "… clipped" once; further clips within five seconds say nothing.
18. **Space** stops the session.

## Around the window

19. **Ctrl+1**, **Ctrl+2**, **Ctrl+3** show the Blocks, Files and Info tabs; Tab through each. On Info, open **Diagnostics** and check it reads.
20. **Settings → Virtual Devices…**: Tab through the window, add a device, rename it (F2), delete it.
21. **Session → All Sessions…**: Tab through the table; Orca reads each column's head and each row.
22. The system tray: open Reverblabr's popover from the panel's tray with the keyboard, and Tab through each session's Run and Stop and the meters listed there.
23. **View → Show as List**: Tab into the list once, then Up, Down, Home and End; Orca reads each lane's heading and each block's name, state and wires. Enter opens a block's settings, C wires it on, Delete takes one out. Turn the list off again.

When every step is done and whatever came up is fixed or filed, the walkthrough has passed.

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Recording

## The Recorder block

Put a **Recorder** (under *Outputs*) anywhere in a lane. It writes what reaches it to a file and passes the sound on unchanged, so the lane can continue to your speakers after it. Several Recorders can hang off the same sound, each writing its own file.

- **It records while the session runs.** The tile shows the format, a red **REC** with the time, and **Pause** and **Split** buttons. Split ends the current file and starts the next one, numbered on. Pause leaves the paused time out and joins the two parts.
- **The file name** is built from **Session**, **Date**, **Time** and **Counter** pieces plus your own text, and the settings show what the next file will be called. Files go to a Reverblabr folder in your Music folder unless you choose another.
- **Quality** offers Voice, Standard and High-quality MP3, High-quality AAC, Lossless (FLAC) and Uncompressed (WAV). **Format options** let you set the bit rate, bit depth, sample rate and mono by hand.
- **AAC and Apple Lossless** are only offered if your system's FFmpeg can write them. On Fedora the full FFmpeg comes from RPM Fusion. FLAC and the other formats always work.
- **Tags** (title, artist, album and so on) are written into each file when it's finished. The name pieces work in tags too, so a Track of `%counter` numbers the files of a split recording.
- **Limits** start a new file every so many minutes, hours or megabytes, stop the recording after a while, or act on silence: leave it out, start a new file after it, or stop.
- **A crash doesn't cost you the recording.** The file plays as it is up to the last few seconds, and Reverblabr restores the rest the next time it starts.

Before you record anyone but yourself, read [Privacy](privacy.md).

## Your recordings

The **Files** tab of the sidebar (Ctrl+2) lists what the session has recorded, newest first, with each file's name, its outline, its length, format and size. A file still being recorded has a red dot.

To listen to a recording, select it and press its play button, or Space. Click the outline to listen from that point. It plays on your default output like any music player would, and a running session carries on as it was. Right-click a recording for more:

- **Open Containing Folder** opens your file manager with the file selected.
- **Open With…** offers the programs that can open it, such as an audio editor.
- **Move to Trash** moves it to the trash, where you can restore it from. Delete does the same.

Double-click a recording, or press Enter, to open it in the **Recording Inspector**, a window of its own. You can rename the file there, fill in its tags and press **Save Tags** to write them into the file. Space plays and pauses, and the arrow keys jump five seconds back or forward.

A file you delete or move in your file manager disappears from the list the next time you open the tab.

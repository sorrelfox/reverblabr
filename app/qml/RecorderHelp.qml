// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Recorder block's help page (docs/dev/block-help.md). The facts are
// the block's parameters (engine/blocks/recorder.cpp), the writer's rules
// (engine/record/recordingwriter.h: limits and silence; filename.h: the
// name's pieces), and the panel's words (RecorderPanel, RecorderWords).
HelpPage {
    typeId: "output.recorder"
    summary: KI18n.i18nc("@info help page of the Recorder block", "Writes the sound that reaches it to a file while the session runs, and passes the sound on unchanged.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Put a Recorder anywhere in a lane. When you start the session, it starts a file; when you stop the session, it finishes the file. The line at the top of its settings says what it is doing, and for how long."),
            KI18n.i18nc("@info help page of the Recorder block", "The sound also goes on through the block as it is, with no delay, so the lane can carry on to your speakers after it. Several Recorders can take the same sound, each writing a file of its own."),
            KI18n.i18nc("@info help page of the Recorder block", "Most settings are read when a file starts. A change made while it records takes effect with the next file."),
            KI18n.i18nc("@info help page of the Recorder block", "Your recordings are listed in the Files tab of the sidebar. Listen to them there, or open one to rename it or change its tags.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title a part of the Recorder's help page", "While it records")
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button pause a recording", "Pause")
        covers: [KI18n.i18nc("@action:button resume a paused recording", "Resume")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Stops writing for a while; Resume carries on in the same file. The time it was paused is left out, and the two parts follow on from each other.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@action:button end this recording's file and start the next", "Split file")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Finishes the file it is writing and starts the next one at once, with nothing lost in between. Use it to start a new song, a new speaker or a new chapter.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Recorder's settings", "General")
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label a recording's file name", "File name")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "What each file is called. Type your own words, and add pieces that are filled in when the file starts, with the buttons under the box:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Recorder block, a piece of a file name", "Session: the session's name."),
            KI18n.i18nc("@info help page of the Recorder block, a piece of a file name; keep the date as it is", "Date: the day, written year first, such as 2026-09-25."),
            KI18n.i18nc("@info help page of the Recorder block, a piece of a file name; keep the time as it is", "Time: the time of day, such as 14-03-22. Hyphens rather than colons, which many memory sticks refuse in a name."),
            KI18n.i18nc("@info help page of the Recorder block, a piece of a file name", "Counter: a number that counts up from 001, to keep names apart."),
            KI18n.i18nc("@info help page of the Recorder block, a piece of a file name", "Block: the name you gave this Recorder. Without one, it is a short code instead.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Recorder block", "The line under the box shows what the next file will be called. A new Recorder names its files after the session, the date and the time."),
            KI18n.i18nc("@info help page of the Recorder block", "A recording never replaces a file that is already there. When a name is taken, a number in brackets is added after it, such as (2).")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label the folder recordings go to", "Save to")
        covers: [
            KI18n.i18nc("@action:button choose the folder recordings go to", "Choose Folder…"),
            KI18n.i18nc("@action:button open the folder recordings go to in the file manager", "Show in File Manager")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "The folder the files go to. Left empty, it is a folder called Reverblabr in your Music folder; the box shows which in grey."),
            KI18n.i18nc("@info help page of the Recorder block", "The folder button next to it lets you pick another folder, and the button after it opens the folder in your file manager.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox how good a recording is, and how big", "Quality")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "The easy way to choose how the file is made. Each choice sets the format and its details together, and the line under the list says what you get:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Recorder block, a recording quality", "Voice: small files for speech, in one channel."),
            KI18n.i18nc("@info help page of the Recorder block, a recording quality", "Standard MP3: good for most listening."),
            KI18n.i18nc("@info help page of the Recorder block, a recording quality", "High-quality MP3: what a new Recorder uses. It sounds as good as MP3 gets for most ears, and plays anywhere."),
            KI18n.i18nc("@info help page of the Recorder block, a recording quality", "High-quality AAC: smaller than MP3 for the same sound."),
            KI18n.i18nc("@info help page of the Recorder block, a recording quality", "Lossless: every bit of the sound kept, in about half the space."),
            KI18n.i18nc("@info help page of the Recorder block, a recording quality", "Uncompressed: every bit kept, in the biggest files, for editing afterwards.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@title:group a recording's format and its settings", "Format options")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "The details Quality sets for you, to change one by one. Click the heading to open them. Once you change one, Quality says Custom.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox a recording's file format", "Format")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "The kind of file. They come in three families:")
        ]
        points: [
            // i18n: the format names are the same in every language.
            KI18n.i18nc("@info help page of the Recorder block, a family of formats", "MP3, Opus, Ogg Vorbis and AAC make small files by leaving out details the ear is unlikely to miss. This is called lossy compression."),
            KI18n.i18nc("@info help page of the Recorder block, a family of formats", "FLAC and ALAC keep every bit of the sound and still pack it smaller. This is lossless compression."),
            KI18n.i18nc("@info help page of the Recorder block, a family of formats", "WAV and AIFF keep the sound as it is, uncompressed, in the biggest files.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Recorder block", "AAC and ALAC are only offered when your system's audio libraries can write them; a line under the options says so when they cannot. The other formats always work."),
            KI18n.i18nc("@info help page of the Recorder block", "An AIFF file cannot grow past 4 GB, so an AIFF recording starts a new file by itself at 2 GB or after 24 hours.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox how many bits a second a recording keeps", "Bit rate")
        covers: [
            KI18n.i18nc("@action:button", "Variable bit rate"),
            KI18n.i18nc("@action:button", "Constant bit rate")
        ]
        paragraphs: [
            // i18n: kbps stands for kilobits per second.
            KI18n.i18nc("@info help page of the Recorder block", "For MP3, Opus, Ogg Vorbis and AAC: how much of the sound each second keeps, from 32 to 320 kilobits a second (kbps). More sounds better and makes bigger files. A new Recorder uses 256 kbps."),
            KI18n.i18nc("@info help page of the Recorder block", "VBR, a variable bit rate, spends more on busy passages and less on quiet ones; it is the usual choice. CBR, a constant bit rate, spends the same every second, which some old players need. AAC always uses a constant rate.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox how many bits each sample of a recording keeps", "Bit depth")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "For WAV, AIFF, FLAC and ALAC: how finely each moment of the sound is measured. 16-bit is CD quality. 24-bit, what a new Recorder uses, leaves room for quiet recordings to be turned up later. 32-bit float can never clip in the file, which helps if you edit it afterwards; FLAC and ALAC do not offer it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox a recording's sample rate", "Sample rate")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "How many times a second the sound is measured. Follow session, the choice for a new Recorder, uses whatever rate the session runs at. 44.1 kHz is the rate of a CD and 48 kHz that of video; 88.2 kHz and 96 kHz are for studio work."),
            KI18n.i18nc("@info help page of the Recorder block", "A rate other than the session's is converted as the file is written. Opus cannot be 44.1 kHz, so an Opus file that follows a 44.1 kHz session is made at 48 kHz.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label how many channels a recording has", "Channels")
        covers: [
            KI18n.i18nc("@item two channels in a recording", "Stereo"),
            KI18n.i18nc("@item one channel in a recording", "Mono")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Stereo keeps the left and the right side apart. Mono mixes the two into one channel, which is all a voice needs and makes the file smaller.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Recorder's settings", "Tags")
        covers: [
            KI18n.i18nc("@label a recording's tag", "Title"),
            KI18n.i18nc("@label a recording's tag", "Artist"),
            KI18n.i18nc("@label a recording's tag", "Album"),
            KI18n.i18nc("@label a recording's tag", "Genre"),
            KI18n.i18nc("@label a recording's tag", "Year"),
            KI18n.i18nc("@label a recording's tag", "Track"),
            KI18n.i18nc("@label a recording's tag, how many tracks there are", "Of tracks"),
            KI18n.i18nc("@label a recording's tag", "Disc"),
            KI18n.i18nc("@label a recording's tag, how many discs there are", "Of discs"),
            KI18n.i18nc("@label a recording's tag", "Comment")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Tags are the details a music player shows about a file: its title, artist, album, genre, year, track and disc numbers, and a comment. Whatever you fill in here is written into each file when it is finished."),
            // i18n: %session, %date, %year and %counter are typed as they are and must not be translated.
            KI18n.i18nc("@info help page of the Recorder block", "The pieces of a file name work here too: type %session, %date, %year or %counter. A Track of %counter numbers the files of a recording that was split.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Recorder's settings", "Limits")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Rules for when a recording splits into files or stops by itself. Each is off until you tick it.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check followed by a number and a unit", "Start a new file every")
        covers: [KI18n.i18nc("@label:listbox", "Unit")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Splits a long recording into files of the same length, in minutes or hours, or of the same size, in megabytes (MB). Nothing is lost between two files. When you tick it, it starts at 30 minutes.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check followed by a number and a unit", "End the recording after")
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Stops the recording by itself after so many minutes or hours of it, counted across all its files and not while it is paused. When you tick it, it starts at 1 hour. The session goes on running; the line at the top then says the recording stopped at its time limit.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check", "Act on silence")
        covers: [
            KI18n.i18nc("@label:listbox", "What silence does"),
            KI18n.i18nc("@label:spinbox", "Seconds of silence")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "Tick the box at the start of the row to do something once the sound has been silent for a while. Choose what in the list, and how many seconds of silence it waits for in the box after it: 2 seconds for a new Recorder.")
        ]
        points: [
            KI18n.i18nc("@info help page of the Recorder block, a choice of what silence does", "Remove silence: the silence after those seconds is left out of the file, until sound comes back."),
            KI18n.i18nc("@info help page of the Recorder block, a choice of what silence does", "Split on silence: the file ends after those seconds, and the next one starts when sound comes back. Good for recording songs from a playlist into a file each."),
            KI18n.i18nc("@info help page of the Recorder block, a choice of what silence does", "Stop on silence: the recording ends after those seconds.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider the level below which a recording counts as silent", "Silence below")
        covers: [KI18n.i18nc("@label:slider", "Silence threshold")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Recorder block", "How quiet the sound must be, on every channel, to count as silence: from −96 dB, near-total silence, to −20 dB, which takes in a fair amount of background noise. A new Recorder uses −60 dB."),
            KI18n.i18nc("@info help page of the Recorder block", "If the hiss of a quiet room keeps your recording from ever counting as silent, move the slider to the right.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Recorder block", "Recording a call, a lesson or a podcast from the programs that play it."),
            KI18n.i18nc("@info help page of the Recorder block", "Recording a microphone, with the blocks that clean it up placed before the Recorder."),
            KI18n.i18nc("@info help page of the Recorder block", "Keeping a long broadcast as a file every hour, or recording music into a file a song.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Recorder block", "Put a Peak/RMS meter just before the Recorder and check the level before you start."),
            KI18n.i18nc("@info help page of the Recorder block", "Choose Voice for speech you only need to listen to again, and Lossless or Uncompressed for anything you will edit."),
            KI18n.i18nc("@info help page of the Recorder block", "If Reverblabr or the computer stops suddenly, the file still plays up to the last few seconds, and Reverblabr restores the rest the next time it starts."),
            KI18n.i18nc("@info help page of the Recorder block", "Before you record anyone but yourself, ask them first.")
        ]
    }
}

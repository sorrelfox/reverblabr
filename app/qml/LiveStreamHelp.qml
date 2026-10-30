// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Live Stream block's help page (docs/dev/block-help.md). The facts
// are the block's parameters (engine/blocks/livestream.cpp), the stream's
// (engine/stream/livestream.h and .cpp: the presets and their bit rates,
// what connects again, the frames left out), the encoders'
// (engine/stream/videoencoder.h: the order they are tried in), the
// picture's (engine/stream/compositor.h), and the panel's words
// (LiveStreamPanel, LiveStreamWords, NowPlayingRows). What a radio stream
// shares with it (the retries, the title template) is on the Broadcast
// block's page and its sources.
HelpPage {
    typeId: "output.livestream"
    summary: KI18n.i18nc("@info help page of the Live Stream block", "Sends the sound that reaches it to a video streaming service, with a picture made for it, and passes the sound on unchanged.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "Services that take a live stream expect video, even for a radio show or a set of music. So the block draws a picture for the sound: a title, a line or two of words, what is playing, and something that moves with the sound, over a colour or a picture of your own."),
            // i18n: RTMP and RTMPS are the names of protocols, the same in every language.
            KI18n.i18nc("@info help page of the Live Stream block", "It sends both to the service over RTMP or RTMPS, the way most streaming services take a live stream. You need the service's address and your stream key, from the service's page for going live."),
            KI18n.i18nc("@info help page of the Live Stream block", "The block connects when you start the session and lets go when you stop it. The sound also goes on through the block as it is, so the lane can carry on to your speakers or a Recorder."),
            KI18n.i18nc("@info help page of the Live Stream block", "The picture keeps time with the sound however long the stream runs. If the computer is too busy to draw every frame, it leaves some out rather than let the sound wait."),
            KI18n.i18nc("@info help page of the Live Stream block", "The line at the top of its settings says what it is doing. If the service or the network goes away, the block tries again after a second, then waits twice as long each time, up to a minute. A missing stream key, or a computer that cannot make the video, stops it until you change something.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Live Stream block's settings", "Setup")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "Where the stream goes, and how big its picture is. A change here while it is on the air makes the block connect again.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox a video service's RTMP address", "Server")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "The service's address for live streams, copied as the service gives it. It starts with rtmps:// or rtmp://. A new block has none, so nothing is sent anywhere until you fill it in."),
            KI18n.i18nc("@info help page of the Live Stream block", "Choose the rtmps:// address where the service offers one. The stream is then encrypted on its way, and the block checks that the server is really the one named.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox the secret key a video service gives for streaming", "Stream key")
        covers: [KI18n.i18nc("@action:button remove a live stream's stored key", "Forget Stream Key")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "The secret code that tells the service the stream is yours. Paste it and press Enter. Anyone who has it can stream on your channel, so keep it to yourself."),
            KI18n.i18nc("@info help page of the Live Stream block", "It is kept in your desktop's password store, never in the session file. The box then empties and says Stored; type in it again to replace the key. The button beside it forgets the key altogether.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox a live stream's size and frame rate", "Quality")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "How big the picture is, and how many times a second it changes. A bigger picture needs a faster upload:")
        ]
        points: [
            // i18n: kbps stands for kilobits per second.
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Quality", "Good: 1024 × 576, 15 frames a second. For a slow connection; it sends about 900 kbps in all."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Quality", "Higher: 1280 × 720, 30 frames a second, what a new block uses. It sends about 2,600 kbps."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Quality", "Best: 1920 × 1080, 30 frames a second. It sends about 4,800 kbps.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Live Stream block", "The better the quality, the better the sound as well: Best sends the sound at 320 kbps, Higher at 128 and Good at 96.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox what makes a live stream's video", "Encoder")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "What turns the pictures into video. The list offers only the ones that work on this computer:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Encoder", "The best one here: what a new block uses. It tries the graphics card first, then the others."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Encoder", "A graphics card: the card does the work, which costs the processor almost nothing."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Encoder", "x264 and OpenH264: the processor does the work, which can keep a slow computer busy.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Live Stream block", "If the encoder you chose will not start, the block uses the next one that does. If none works, a line at the top of the tab says what to install.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Live Stream block's settings", "Picture")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "What the picture shows. A change here only redraws it; the stream goes on.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox the large words on a live stream's picture", "Title")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "The large words on the picture. A new block shows the session's name, which is what %session stands for; type your own words instead.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox the words under a live stream's title", "Subtitle")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "A smaller line under the title, such as the name of the show or when it is on. Leave it empty for none.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox how what is playing is shown on a live stream's picture", "Now playing as")
        paragraphs: [
            // i18n: %artist, %title, %album and %session are typed as they are and must not be translated.
            KI18n.i18nc("@info help page of the Live Stream block", "The line that says what is playing. Type your own words with the pieces %artist, %title and %album, filled in from Now playing below, and %session, the session's name. A new block uses %artist - %title."),
            KI18n.i18nc("@info help page of the Live Stream block", "Words between two pieces are left out when either is empty, so a track with no artist shows its title alone. When nothing is known, the line is left off the picture."),
            KI18n.i18nc("@info help page of the Live Stream block", "Once the stream is on the air, a line under Now playing shows what the picture says now.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox where a stream's title comes from", "Now playing")
        covers: [
            KI18n.i18nc("@label:textbox the text file a stream's title is read from", "File"),
            KI18n.i18nc("@action:button choose the text file a stream's title is read from", "Choose File…"),
            KI18n.i18nc("@label:listbox the media player a stream's title follows", "Player")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "Where the block learns what is playing, as a Broadcast block does:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Now playing", "Only the template: nothing is learned. What a new block uses."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Now playing", "A text file: its first line is the title. Many music players can write such a file. Type its place in the File box, or choose it with the button beside it."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Now playing", "A media player: the block asks a player on this computer what it is playing. The one this session captures follows a player whose sound the session's Application blocks take, or whichever is playing when they take none. Or choose a player by name.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox what moves with the sound in a live stream's picture", "Moving")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "What moves with the sound, under the words, so viewers can see the stream is live:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Moving", "Waveform: the shape of the sound as a moving line. What a new block shows."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Moving", "Spectrum: bars that rise and fall with the low, middle and high sounds."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Moving", "Level meter: a bar across for each side, showing how loud it is."),
            KI18n.i18nc("@info help page of the Live Stream block, a choice of Moving", "Nothing: a still picture.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox a live stream's background colour", "Background")
        paragraphs: [
            // i18n: keep the colour code #16181d as it is.
            KI18n.i18nc("@info help page of the Live Stream block", "The colour behind everything, as a colour code: a # and six letters and numbers, such as #16181d, the dark grey a new block uses. The square beside the box shows the colour.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox a picture covering a live stream's frame", "Background picture")
        covers: [KI18n.i18nc("@action:button choose a picture for a live stream", "Choose Picture…")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "A picture to cover the whole frame instead of the colour. Type the file's place, or choose it with the button beside the box. Leave it empty for none.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox a picture at the corner of a live stream's frame", "Logo")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "A small picture at the top left, such as the logo of your show. Choose it as you choose the background picture."),
            KI18n.i18nc("@info help page of the Live Stream block", "The words are light or dark, whichever reads best over what is behind them, with a shade around them so that a busy picture does not swallow them.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Live Stream block's settings", "Status")
        paragraphs: [
            KI18n.i18nc("@info help page of the Live Stream block", "How the stream is going, while the session runs. Nothing here can be changed:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Live Stream block, a line of the Status tab", "Connected for, Sent and Upload: how long it has been on the air, how much it has sent, and at what rate."),
            KI18n.i18nc("@info help page of the Live Stream block, a line of the Status tab", "Video: the picture's size, its frames a second and the encoder at work."),
            KI18n.i18nc("@info help page of the Live Stream block, a line of the Status tab", "Frames: how many frames it has sent, and how many it left out because the computer fell behind. If that second number keeps growing, choose a lower quality or another encoder."),
            KI18n.i18nc("@info help page of the Live Stream block, a line of the Status tab", "Reconnects and Last problem: how many times it had to connect again, and what last went wrong.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Live Stream block", "You want to stream a radio show, a podcast or music to a video service, with no camera."),
            KI18n.i18nc("@info help page of the Live Stream block", "You want viewers to see the title of each song as it plays.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Live Stream block", "If the service lets you stream privately first, try it there, and watch it on another device, before you go public."),
            KI18n.i18nc("@info help page of the Live Stream block", "Put a Peak/RMS meter before the block and keep the level out of the red: viewers hear any crackle too."),
            KI18n.i18nc("@info help page of the Live Stream block", "Start with Higher. If the Frames line on the Status tab shows frames left out, or your connection struggles, choose Good."),
            // i18n: openh264 is the name of a package, typed as it is.
            KI18n.i18nc("@info help page of the Live Stream block", "If the computer has no encoder that works, install Cisco's OpenH264, on Fedora the openh264 package, or the full FFmpeg.")
        ]
    }
}

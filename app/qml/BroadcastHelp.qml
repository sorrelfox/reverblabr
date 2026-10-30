// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

import QtQuick
import org.kde.ki18n

// The Broadcast block's help page (docs/dev/block-help.md). The facts are
// the block's parameters (engine/blocks/broadcast.cpp), the stream's rules
// (engine/stream/broadcaststream.h and .cpp: the retries, the SHOUTcast
// port; connection.cpp: TLS and the certificate check; backoff.h: the waits; streamencoder.cpp: the bit rates each format
// takes; title.h: the template and the file), the player it follows
// (engine/mpris/player.h, choosePlayer) and the panel's words
// (BroadcastPanel, BroadcastWords, NowPlayingRows).
HelpPage {
    typeId: "output.broadcast"
    summary: KI18n.i18nc("@info help page of the Broadcast block", "Sends the sound that reaches it to an internet radio server, for anyone to listen to, and passes the sound on unchanged.")

    HelpSection {
        title: BlockHelp.aboutTitle
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "An internet radio station is a server that listeners connect to. This block is the source that feeds it: it turns the sound into a stream, in the format you choose, and sends it to the server, which hands it on to every listener."),
            // i18n: Icecast and SHOUTcast are the names of programs, the same in every language.
            KI18n.i18nc("@info help page of the Broadcast block", "It works with the two common kinds of radio server, Icecast and SHOUTcast. You need the server's address and a password for it, from whoever runs it."),
            KI18n.i18nc("@info help page of the Broadcast block", "The block connects when you start the session, and lets go when you stop it. The sound also goes on through the block as it is, so the lane can carry on to your speakers or a Recorder."),
            KI18n.i18nc("@info help page of the Broadcast block", "The line at the top of its settings says what it is doing: waiting for the session, connecting, on the air with how long for, or what went wrong."),
            KI18n.i18nc("@info help page of the Broadcast block", "If the server or the network goes away, the block tries again after a second, then waits twice as long each time, up to a minute. The sound in between is not kept: when it is back, listeners hear what is playing then."),
            KI18n.i18nc("@info help page of the Broadcast block", "Some problems need you instead: a wrong or missing password, a server certificate that is refused, or a format this computer cannot make. The block stops trying until you change the setting, and the line at the top says why.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Broadcast block's settings", "Setup")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "Where the stream goes, and what it sounds like. A change here while it is on the air makes the block connect again, which listeners may hear as a short break.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox the kind of internet radio server", "Server")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "The kind of server: Icecast, what a new block uses, or SHOUTcast. Whoever runs the server can tell you which it is.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox a server's name or address", "Host")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "The server's name or address, such as radio.example.org. A new block says localhost, which is this computer, for a server you run yourself.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:spinbox a server's port", "Port")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "The number the server listens on, from 1 to 65535. A new block uses 8000, the usual one. A SHOUTcast server takes its stream on the number after the one you give, as SHOUTcast always does, so give the number its listeners use.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check send a stream to its server over TLS", "Secure connection (TLS)")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "For Icecast only: sends the stream, its title and the password encrypted with TLS, as a web browser's https does. Without it they cross the network as they are, which is fine for a server on this computer or your own network, and not for one across the internet. It is off for a new block."),
            KI18n.i18nc("@info help page of the Broadcast block", "A server takes TLS on a port of its own, often 8443, so change the Port too. Whoever runs the server can tell you whether it offers TLS, and on which port. SHOUTcast servers take no TLS, so the box is not offered for them."),
            KI18n.i18nc("@info help page of the Broadcast block", "The block checks the server's certificate the way a web browser does: it must be from an authority this computer trusts, made out to the Host you typed, and in date. If it is not, the block does not send the password, stops trying, and the line at the top says which of the three is wrong. There is no way to skip the check. For a server with a certificate of its own making, add that certificate to this computer's trusted ones, as for any other program.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox an Icecast server's mount point", "Mount point")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "For Icecast only: the name of your stream on the server, which ends the address listeners open. A new block uses /stream. One server can carry several streams, each at a mount point of its own.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox the user a stream logs in to its server as", "User")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "For Icecast only: the name the block logs in with. It is nearly always source, what a new block uses.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox the password a stream logs in to its server with", "Password")
        covers: [KI18n.i18nc("@action:button remove a stream's stored password", "Forget Password")]
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "Type the password and press Enter. It is kept in your desktop's password store, never in the session file, so a session you copy or send to somebody carries no password."),
            KI18n.i18nc("@info help page of the Broadcast block", "The box then empties and says Stored. Type in it again to replace the password. The button beside it forgets the password altogether.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox a stream's format", "Format")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "How the sound is packed for the trip. Each makes it much smaller by leaving out details the ear is unlikely to miss:")
        ]
        points: [
            // i18n: the format names are the same in every language.
            KI18n.i18nc("@info help page of the Broadcast block, a stream format", "MP3: what a new block uses. Every player and every server takes it."),
            KI18n.i18nc("@info help page of the Broadcast block, a stream format", "Opus: the best sound for its size, especially at low bit rates."),
            KI18n.i18nc("@info help page of the Broadcast block, a stream format", "Ogg Vorbis: an open format that most players take."),
            KI18n.i18nc("@info help page of the Broadcast block, a stream format", "AAC: smaller than MP3 for the same sound, and played by most phones.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Broadcast block", "If this computer's audio libraries cannot make a format, a line under the list says which.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:listbox a stream's bit rate", "Bit rate")
        paragraphs: [
            // i18n: kbps stands for kilobits per second.
            KI18n.i18nc("@info help page of the Broadcast block", "How much of the sound each second keeps, from 32 to 320 kilobits a second (kbps). More sounds better, and every listener needs a connection fast enough to take it. A new block uses 128 kbps, plenty for music in MP3."),
            KI18n.i18nc("@info help page of the Broadcast block", "The list offers only what the format can do: Ogg Vorbis starts at 48 kbps in stereo and stops at 192 kbps in mono, and Opus in mono stops at 256 kbps.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label stereo or mono", "Channels")
        covers: [
            KI18n.i18nc("@action:button two channels", "Stereo"),
            KI18n.i18nc("@action:button one channel, both sides mixed", "Mono")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "Stereo, what a new block uses, keeps the left and the right side apart. Mono mixes them into one, which is all speech needs and leaves more of the bit rate for the sound itself.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Broadcast block's settings", "Metadata")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "What the server and the listeners are told about the station, and the title of what is playing.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox a radio station's name", "Station")
        covers: [
            KI18n.i18nc("@label:textbox a radio station's genre", "Genre"),
            KI18n.i18nc("@label:textbox a radio station's web page", "Web page"),
            KI18n.i18nc("@label:textbox a radio station's description", "Description")
        ]
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "The station's name, its genre, the address of its web page and a line describing it. Listeners' players and the server's own pages can show them. All of them can be left empty. A change to them while on the air makes the block connect again.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@option:check let the server list a station in a public directory", "List in public directories")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "Lets the server add the station to the public lists of internet radio, where anyone can find it. It is off for a new block.")
        ]
    }

    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:textbox how the title listeners see is made", "Title")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "The title listeners see for what is playing. Type your own words, with any of these pieces, which are filled in as the music changes:")
        ]
        points: [
            // i18n: %artist, %title, %album, %session and %% are typed as they are and must not be translated.
            KI18n.i18nc("@info help page of the Broadcast block, a piece of the title", "%artist, %title and %album: the artist, the title and the album of the track that is playing, from Now playing below."),
            KI18n.i18nc("@info help page of the Broadcast block, a piece of the title", "%session: the session's name."),
            KI18n.i18nc("@info help page of the Broadcast block, a piece of the title", "%%: a percent sign.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Broadcast block", "A new block uses %artist - %title. Words between two pieces are left out when either is empty, so a track with no artist shows its title alone, with no dash in front."),
            KI18n.i18nc("@info help page of the Broadcast block", "Once the stream is on the air, a line at the end of the tab shows the title listeners see now.")
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
            KI18n.i18nc("@info help page of the Broadcast block", "Where the block learns what is playing, for the pieces of the title:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Broadcast block, a choice of Now playing", "Only the template: nothing is learned, and the title is your own words and %session. What a new block uses."),
            KI18n.i18nc("@info help page of the Broadcast block, a choice of Now playing", "A text file: the first line of the file is the title. Many music players can write such a file. Type its place in the File box, or choose it with the button beside it. The block looks at the file every second and sends a new title when it changes."),
            KI18n.i18nc("@info help page of the Broadcast block, a choice of Now playing", "A media player: the block asks a music or video player on this computer what it is playing, with the artist and the album. Choose which in the Player list.")
        ]
        closing: [
            KI18n.i18nc("@info help page of the Broadcast block", "The one this session captures, the first choice of Player, follows a player that the session's Application blocks take the sound of, so the title is that of the music listeners hear. In a session that takes no program, such as one with a microphone or System Audio, it follows whichever player is playing. Of several, one that is playing comes before one that is paused, and the one that started last before the others."),
            KI18n.i18nc("@info help page of the Broadcast block", "A player chosen by name is followed whatever the session takes. While it is not running, the list says so and keeps it chosen. When the player stops, only the words of the title that do not need it are left.")
        ]
    }

    HelpSection {
        title: KI18n.i18nc("@title:tab a Broadcast block's settings", "Status")
        paragraphs: [
            KI18n.i18nc("@info help page of the Broadcast block", "How the stream is going, while the session runs. Nothing here can be changed:")
        ]
        points: [
            KI18n.i18nc("@info help page of the Broadcast block, a line of the Status tab", "Connected for: how long it has been on the air this time."),
            KI18n.i18nc("@info help page of the Broadcast block, a line of the Status tab", "Connection: Secure (TLS) while the stream goes encrypted, Not encrypted while it does not."),
            KI18n.i18nc("@info help page of the Broadcast block, a line of the Status tab", "Sent: how much it has sent to the server."),
            KI18n.i18nc("@info help page of the Broadcast block, a line of the Status tab", "Bit rate: the rate it is sending at, as measured."),
            KI18n.i18nc("@info help page of the Broadcast block, a line of the Status tab", "Reconnects: how many times it had to connect again. A number that keeps growing means a poor connection or a server in trouble."),
            KI18n.i18nc("@info help page of the Broadcast block, a line of the Status tab", "Last problem: what last went wrong, even once it has been put right.")
        ]
    }

    HelpSection {
        title: BlockHelp.whenTitle
        points: [
            KI18n.i18nc("@info help page of the Broadcast block", "You run an internet radio station, or a stream for a club, a church or a school, and want to feed it from this computer."),
            KI18n.i18nc("@info help page of the Broadcast block", "You want to send a live event to listeners who open it in a browser or a radio app."),
            KI18n.i18nc("@info help page of the Broadcast block", "You want the music you play here to reach a server with the right titles on it.")
        ]
    }

    HelpSection {
        title: BlockHelp.tipsTitle
        points: [
            KI18n.i18nc("@info help page of the Broadcast block", "Put a Peak/RMS meter before the block and keep the level out of the red: listeners hear any crackle too. A Limiter just before it is a good safeguard."),
            KI18n.i18nc("@info help page of the Broadcast block", "Before going live, listen to the stream yourself from the server's address, on another device."),
            KI18n.i18nc("@info help page of the Broadcast block", "If the line at the top says the server refused the user name or password, check the User and the Password. For SHOUTcast, check the port, too."),
            KI18n.i18nc("@info help page of the Broadcast block", "If it says no secure connection could be made, the port is most likely the server's plain one: ask for its TLS port. If it says the certificate is for another name, type the Host as the server's certificate names it, usually its full name rather than its address."),
            KI18n.i18nc("@info help page of the Broadcast block", "If your connection is slow, choose a lower bit rate, or Mono for speech, before you lower anything else.")
        ]
    }
}

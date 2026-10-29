<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Privacy

Linux doesn't ask for permission before one program listens to another program or to your microphone. Reverblabr can capture any application, everything the computer plays and any microphone, and nobody is asked. That puts the responsibility on you.

- **Other people's voices.** In many countries it is against the law to record a phone or video call, or any conversation, without the agreement of everyone in it, and the rules differ from place to place. Find out what applies where you and the other people are, and ask before you record.
- **What you capture.** System Audio takes everything that plays, including calls, notifications and whatever a browser tab is doing. Leave out the programs you don't mean to capture.
- **Seeing who is listening.** A running session shows up by name in PipeWire's own tools, such as `wpctl status`, qpwgraph and Helvum, along with what it is connected to. The Plasma audio applet doesn't list it, so look there if you need to know.

Reverblabr itself doesn't use the network at all: no usage statistics, no update checks. An Application block can be set to open a web address when its session starts, but that address is opened by your browser or player, not by Reverblabr. When you copy diagnostics for a bug report, your home folder, user name and computer name are left out, but please read the text before you send it.

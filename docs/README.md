<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Reverblabr documentation

These pages explain how to build Reverblabr, how to use it, and how it is put together inside. The program is still in development, so things described here can change, and some rough edges aren't described at all yet. If a page and the program disagree, one of them is wrong, and I'd like to know about it.

## Using Reverblabr

- [Installing](installing.md): what you need, how to build it, and how to install it
- [Getting started](getting-started.md): your first session, from an application to your speakers
- [Blocks](blocks.md): every block in the library and what it is for
- [Virtual devices](virtual-devices.md): sound cards of your own, for routing programs through Reverblabr
- [Hearing](hearing.md): shaping the sound for each ear, if you hear some frequencies poorly
- [Recording](recording.md): the Recorder block and the files it makes
- [On the desktop](desktop.md): the tray icon, global shortcuts, KRunner, sessions that start at login, notifications
- [Privacy](privacy.md): what Reverblabr can hear, and recording other people
- [When something goes wrong](troubleshooting.md): diagnostics, common problems and bug reports

## Scripting

- [reverblabr-ctl](ctl.md): the command-line tool, its commands, its JSON output and its exit codes
- [The D-Bus API](dbus-api.md): driving Reverblabr from your own scripts and programs

## Working on Reverblabr

- [Development](development.md): setting up a machine, building, testing and making a release
- [What to install](dependencies.md): every library and tool, and what each one is for
- [Architecture](architecture.md): the layers, processes and threads, and the rules for the audio thread
- [Decision records](adr/README.md): why the big choices were made
- [Conventions](conventions.md): how the code is written, and how commits are described
- [Translations](i18n.md): how user-visible text is written and extracted
- [From an audiogram to band settings](fitting-rules.md): the fitting rules behind the hearing blocks
- [Notes](dev/): measurements and experiments that some of the decisions rest on

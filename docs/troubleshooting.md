<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# When something goes wrong

Reverblabr is still in development and has bugs. This page lists the problems people are most likely to run into, and how to report the ones that aren't here.

## Diagnostics

The **Info** tab (Ctrl+3) has a **Diagnostics** section under *Audio system*. Unfold it to see how each open session is keeping up: its load, how often it was late, and whether it got real-time priority. It also lists what Reverblabr is connected to, which recording formats this computer can write, and how many plugins were found.

**Copy Diagnostics** puts all of that on the clipboard, together with Reverblabr's last log lines, ready to paste into a bug report. Your home folder, user name and computer name are left out, but read it through before you send it.

## Common problems

**I hear the sound twice.** An Application block takes a copy of a program's sound and leaves the original playing. [Getting started](getting-started.md#run-it) shows how to hear only the processed sound, and [Virtual devices](virtual-devices.md#process-the-sound-of-your-programs) has the cleaner way.

**A banner says the background engine isn't installed.** That's expected when you run Reverblabr from the build tree: the window runs the engine itself, and sessions stop when it closes. Install it to get the background engine ([Installing](installing.md#install-it)).

**The sound drops out.** Look at the xrun counter in the status bar and at Diagnostics. A session without real-time priority is much more likely to drop out when the computer is busy, and Diagnostics says so. Real-time priority comes from rtkit, which Fedora and Kubuntu run by default. Heavy plugins, many sessions at once and a very small buffer size in PipeWire make dropouts more likely too.

**Reverblabr refused a connection.** It refuses anything that would feed a session's sound back into its own input, because that would howl louder and louder. This usually means an Output Device is set to *Default output* while the default is the virtual device the session records from. Choose your real speakers by name.

**A block is missing from the library.** Some blocks are only built when their library was found: Speech Denoise needs RNNoise, the CLAP block needs the CLAP SDK, the VST3 block needs the VST3 SDK 3.8 or later. The build's summary lists what it left out, and [What to install](dependencies.md#optional) has the details.

**A plugin stopped.** Plugins run in their own process, so a crash only takes the plugin down. Its block is bypassed and shows a button to restart it. If it keeps happening, try the plugin in another host, and tell me if it works there.

**Starting Reverblabr only brings the old window back.** Only one Reverblabr runs at a time, and starting it again hands over to the one that's running. If an installed Reverblabr is running, a build from the source tree just raises that one, so quit it first from the tray icon's menu.

**Virtual devices are still there after uninstalling.** They belong to PipeWire. [Removing Reverblabr](virtual-devices.md#removing-reverblabr) says which files to delete.

## Logs

To see Reverblabr's debug messages, start it from a terminal like this:

```sh
QT_LOGGING_RULES='reverblabr.*.debug=true' reverblabr
```

The background engine logs to the journal:

```sh
journalctl --user -u reverblabrd
```

## Reporting a bug

Please open an issue at <https://github.com/sorrelfox/reverblabr/issues>. Tell me what you did, what you expected and what happened instead, and paste what **Copy Diagnostics** gives you. Steps that make the problem happen again are the most useful thing you can send, and a session file or a short recording of the sound can help too.

I work on Reverblabr in my free time, so it may take me a while to answer.

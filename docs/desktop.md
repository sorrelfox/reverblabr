<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# On the desktop

Reverblabr is meant to sit in the background and get on with it: sessions keep running with the window closed, the tray shows what's going on, and shortcuts work from anywhere. Most of this needs an installed Reverblabr, because a build started from the source tree has no background engine (see [Installing](installing.md#run-it-from-the-build)).

## All your sessions at once

**Session → All Sessions…** opens a window listing every session you have saved, open or not: what each one captures, how many recordings it has made, how loud it is now, and how long it has been running.

- **Run** or **Stop** a session with the button at the end of its row. To start several together, select them (Ctrl-click or Shift-click) and press **Run** at the top. A session that wasn't open opens in a tab of the main window as it starts.
- Double-click a session to open it in the main window. **New Session** makes one, and **Delete…** deletes the selected one after asking.
- Click a column header to sort by it, drag a header to move its column, and choose which columns show with **Columns**. The window remembers all of it.

The launcher knows about this too: right-click Reverblabr in the application launcher or the task manager for **New Session** and **All Sessions**. There is only ever one Reverblabr running, so starting it again just brings its window forward.

## Sessions that run when you log in

Some sessions you want running all the time, such as a microphone chain for calls or a hearing setup. Switch on **Info → Auto Run: Run when I log in** for such a session, or tick it in **All Sessions…**.

- From then on, the background engine starts that session when you log in, without opening the window, and again if the engine ever has to restart. Closing the window leaves it running.
- Stopping the session stops it until the next login. Undo takes the setting back like any other change.
- When no session is set to run at login, nothing of Reverblabr starts when you log in.
- It needs an installed Reverblabr. In a build run from the source tree, the Info tab says Auto Run can't work there, and the session keeps the setting for later.

## The tray icon

While Reverblabr runs, its icon sits in the system tray. With nothing running it waits among the hidden icons, and it moves into view while a session runs.

- **Click it** to see every saved session, how loud each one is, and **Run** or **Stop** for each. Meters you marked **Show in system tray** in a Peak/RMS or VU meter's settings are listed there as well.
- **Middle-click it** to show or hide the main window. Its menu has **Show Reverblabr**, **All Sessions…** and **Quit**.
- **A red dot** on the icon means something is being recorded. **A warning sign** means something went wrong; click the icon to read what.

Closing the main window doesn't quit Reverblabr while there is a tray. Your sessions keep running and the icon stays. To quit, use **Quit** in the icon's menu. Sessions running in the background engine keep going even then; only sessions running inside the window stop.

## Global shortcuts

Reverblabr's shortcuts work while the window is in the background or hidden in the tray. They start out with no keys assigned, so nothing clashes with your desktop's own shortcuts. Set the keys you want in **System Settings → Keyboard → Shortcuts**, under **Reverblabr**.

- **Run or stop "name"**, one for every saved session. The shortcut follows its session: renaming the session keeps the key, deleting it removes the key. A session that isn't open is opened and started.
- **Show Reverblabr** brings the main window to the front.
- **Input Switch: play A** and **play B**; **Time Shift: pause or play**, **back 10 seconds**, **forward 10 seconds** and **back to live**; **Recorder: start a new file**. These act on every block of that kind in the session the main window shows (or showed last, while it's hidden).

The shortcuts belong to the window's process, so they work while it runs, open or hidden in the tray. Quitting from the tray ends them, even if sessions keep running in the background engine.

## Notifications

Reverblabr tells your desktop about things you would otherwise miss:

- a recording finished, with **Open** and **Show in Folder**
- a device disconnected while a session was using it (the session waits for it and carries on when it's back)
- an application a session captures stopped playing, and started again
- a plugin stopped (its block is bypassed until you restart it)
- the disk under a recording is nearly full
- a connection that would have fed a session's sound back into itself was refused

A device that comes back within a few seconds, or an application that pauses briefly between two sounds, isn't worth a notification, so you won't hear about it. The same thing about the same block is reported at most once a minute.

Each kind of notification is listed under **Reverblabr** in **System Settings → Notifications**, where you can turn it off or give it a sound. Do Not Disturb silences them like any other.

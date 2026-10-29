<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0025: A plugin's own window is shown by its host, as an X11 window, and what is done in it comes back as messages

- **Status:** Accepted
- **Date:** 2026-10-29

## Context

LV2 and CLAP plugins come with windows of their own, and many people would rather use those than a panel built from the plugin's settings. A plugin runs in a `reverblabr-plugin-host` process of its own (ADR-0014), and the engine never loads it. So:

- **Where the window lives.** A plugin's window is its code, calling into the instance that makes the sound, or talking to it through its own ports. It can only be made in the process the plugin is in, which is the host.
- **What it is drawn with.** LSP's LV2 windows are `ui:X11UI`, with `ui:idleInterface`; its CLAP windows offer `CLAP_WINDOW_API_X11`. Hardly any plugin offers Wayland. A Gtk or Qt UI would need that toolkit loaded in the host, which has none and should stay small.
- **Two ways round.** A knob moved in the window has to reach the sound at once and the engine's model (the panel, the session file, undo); one moved in the panel has to reach the window. If a value went to the engine and back, the window would hear the values a drag passed through after the hand had moved on, and jump back.
- **Undo.** The engine's history merges a drag of one knob into one step when the edits come within half a second of each other (`EditHistory::kGesture`). A plugin's window says better than that: LV2 has `ui:touch`, CLAP has `PARAM_GESTURE_BEGIN` and `END`.
- **Presets.** LV2 presets are `pset:Preset` bundles, which every LV2 host reads from `~/.lv2`. CLAP has no folder of the kind: a plugin lists its presets through its preset-discovery factory and loads them itself (`preset-load`).

## Decision

**The host shows the window, on its own main loop, and talks about it over the control socket** (ADR-0014's channel for what is not a number on its way to the audio thread):

| Engine to host | Host to engine |
|---|---|
| OpenUi, CloseUi | UiOpened; UiClosed with why: `closed`, `unavailable`, `failed` |
| | ParamChanged `<id> <value>`, ParamGesture `<id> begin\|end` |
| | Values `edit\|follow` and every value, when the plugin changed many at once |
| ListPresets, LoadPreset, SavePreset | Presets, PresetShown, PresetSaved |

The window is in the host's process, so a window that crashes takes only its host with it, which is bypassed and can be restarted like any host that fails.

**An LV2 window through suil; both formats' windows as X11 windows.** suil loads the UI. A UI that shows itself (`ui:showInterface`) is asked to; an X11 UI is put inside a top-level X11 window the host makes, which on a Wayland desktop is XWayland's. A CLAP window floats when the plugin offers it and is put in the same kind of window otherwise. The host links libX11 for that, and nothing else of a toolkit. Each window opens where it was last, by plugin, kept in `$XDG_STATE_HOME/reverblabr/plugin-windows`.

**A value from the window is the plugin's first, and is never sent back.** The host puts it on a queue of its own to its audio thread, which takes it after the engine's ring in the same cycle, and tells the engine. The engine puts it into the block and the session, and sends it to every client as a patch, but not to the host. A value from the panel goes through the ring as before, and the host tells the window of every value that differs from what it last told it, thirty times a second.

**A gesture the plugin reports is one undo step**, however long it took (`EditHistory::beginGesture()`), up to half a minute's rest; it ends when the plugin says so for that control, when its window goes (closed, failed, its host down), and at an undo or a redo, so a window that never says it let go holds nothing for ever. Values a person's preset led to are one step too; the values a state put back by the engine led to are not anybody's edit, and only follow it into the session.

**Presets are the format's own.** An LV2 plugin's are read through lilv and saved as a bundle in `~/.lv2` named as other hosts name theirs, so they are found there and back. A CLAP plugin's own come from its preset discovery; one saved from a CLAP block is ours, in our preset folder, holding the plugin's id and its state. The host says which of the plugin's presets the settings are, and that they are none once a value moves away.

## Consequences

- suil and libX11 are build dependencies of the host (`suil-devel`, `libX11-devel`).
- **Messages may be larger than a socket takes as one packet** (a list of thousands of presets, a large state): a SOCK_SEQPACKET socket refuses one larger than its buffer, which is about 208 KiB here and cannot be raised past the system's limit without privileges. So a message goes as packets of 60 KiB that the other end puts together again, up to the protocol's 1 MiB, and one that cannot be sent is answered with Refused instead of being lost.
- **A host is told only what is meant for its plugin**: between another plugin being chosen and its host being replaced, values and states for the new plugin stay in the block, and what the old host says is not taken.
- **The display going away is not the host's end**: Xlib's handlers would end the process, so the host's are installed, and the window is reported as failed.
- A plugin's window is an XWayland window on a Plasma Wayland desktop: it cannot be made transient for the main window, which is a Wayland one, and the compositor places it as it places any X11 window. Its size and position are ours to keep.
- A plugin whose only window is a Gtk or Qt one has none here; the panel is its face, as it is for LADSPA.
- A state put in place, which LV2 forbids during `run()`, needs the audio thread kept away from the plugin: `HostProcessor::hold()`, silent for the cycles it lasts. A preset loaded from the panel is one such moment.
- A value moved in the window and one moved in the panel in the same cycle: the window's is heard. Nobody does both at once.
- The window is the owner's desktop check as much as a test: the tests drive a window of our own that needs no display (`tests/lv2`), and a CLAP plugin of our own whose window is a stand-in.

## Alternatives considered

- **The window in the engine or in the GUI process.** It would have to load the plugin there, which ADR-0014 exists to avoid.
- **suil's wrappers for Gtk and Qt UIs.** They need the toolkit in the host. Qt is already rejected for the host (ADR-0014), and the plugins on the machine that matter, LSP's, are X11.
- **Values from the window through the engine and back to the plugin.** One trip less to write, but a drag would be heard a cycle or two late and the window would be told of its own values after it had moved on.
- **Undo by time alone.** A knob held still for longer than half a second in the middle of a drag would be two steps.

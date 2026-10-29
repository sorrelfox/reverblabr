<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0013: Virtual devices as generated PipeWire drop-ins, mirrored by live nodes

- **Status:** Accepted
- **Date:** 2026-09-24

## Context

The virtual devices manager lets the user create two kinds of audio device from a window in Reverblabr:
- **virtual outputs**, which apps play into and a session captures;
- **virtual microphones**, which a session plays into and apps record from.

Each device can be added, deleted, renamed, enabled and disabled. These were settled on 2026-09-24:
- devices exist **whether or not Reverblabr is running**;
- they belong to **the user** by default;
- a device can instead be made for **all users**, which needs root and so asks for it.

[ADR-0002](0002-native-pipewire-and-wireplumber-as-peer.md) says the app needs no WirePlumber scripts or configuration to work, and until now the engine created no PipeWire object but its own filter nodes and links. A device that outlives the app has to live in PipeWire's own configuration, or be recreated by something at every login. That is new ground for this project.

The spike measured the PipeWire 1.6.9 and WirePlumber 0.5.17 behaviour this depends on. The evidence and commands are in [the spike notes](../dev/spike-virtual-devices.md). In short:

| # | Question | Finding |
|---|---|---|
| 1 | A disabled entry | A `context.objects` entry whose `condition` names a property the context lacks is skipped, and its neighbours are created. |
| 2 | Live creation | `pw_core_create_object(core, "adapter", …)` with `factory.name = support.null-audio-sink` and `object.linger = true` makes a device that outlives its creator. **The node carries no `client.id` and no `application.name`**, so nothing ties it to the app but its name and properties. |
| 3 | Destroying | A later client can destroy both a lingering node and a node made from a drop-in. **PipeWire accepts a second node with a `node.name` already in use.** |
| 4 | Renaming | A node's properties can't be changed from outside, so a rename recreates it. **Destroying first** sends the apps playing into it to the default device until the new node appears (4–6 fallback links in each of 3 runs), which would be audible. **Creating the new node first** and then destroying the old one moves them straight across (0 fallback links in 3 runs). Streams that target the device by property or by metadata both follow. |
| 5 | Drop-in precedence | Drop-ins from `/usr/share`, `/etc` and `$XDG_CONFIG_HOME` are **all** loaded, system first; a same-named file does **not** replace another. |
| 6 | A broken file | An entry that fails, **without `flags = [ nofail ]`**, stops the daemon from starting: no sound at all. With `nofail` it is skipped. A syntax error makes PipeWire ignore the whole file, and the daemon keeps running. |

The original plan had one fact wrong. It said a user file with the same name as the system file would override it. Finding 5 shows that's not so, although two names are still the right choice (see below).

## Decision

**Where a device lives.** Each scope has one drop-in, which Reverblabr writes whole and nobody else is expected to edit. That file is the single source of truth for the scope's devices, including disabled ones.

| Scope | File |
|---|---|
| This user | `$XDG_CONFIG_HOME/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices.conf` |
| All users | `/etc/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices-system.conf` |

- The names differ so that each file has exactly one writer (the app, or the helper below), and a PipeWire that did mask same-named files couldn't hide one scope behind the other.
- The directories are created when missing. Neither exists on a fresh Fedora 44.
- The file starts with a header saying it is managed by Reverblabr. A file at that path without the header is **not ours**: it is reported and never overwritten.

**One entry per device:**

```
{ factory = adapter
  args = {
    factory.name              = support.null-audio-sink
    node.name                 = "reverblabr-virtual.<id>"
    node.description          = "<the user's name for it>"
    media.class               = Audio/Sink            # a virtual output
                              # Audio/Source/Virtual  # a virtual microphone
    audio.position            = [ FL FR ]             # or [ MONO ]
    node.virtual              = true
    monitor.channel-volumes   = true
    reverblabr.virtual-device = "user"                # or "system": the marker
  }
  flags = [ nofail ]
  # only on a disabled device:
  condition = [ { reverblabr.show-disabled-virtual-devices = true } ]
}
```

- **`node.name` is `reverblabr-virtual.<id>`.** `<id>` is eight lower-case hex digits taken from a random UUID, unique across *both* files, since both are always loaded.
  - It never changes: a rename changes `node.description` only. Sessions (the Input and Output Device blocks), WirePlumber's default device and the apps' targets all follow `node.name`, so a rename breaks none of them.
  - It deliberately does **not** start with `reverblabr.`. That prefix means our own processing nodes ([ADR-0006](0006-application-id-and-naming.md)), which the engine hides and which a user's rule may hide from a volume applet. These devices are meant to be seen and picked.
- **`reverblabr.virtual-device` marks our devices** and says which file the device comes from. Nothing else identifies them: finding 2 shows the node names no client.
- **`node.virtual = true`**: `support.null-audio-sink` doesn't set it, and without it the inventory can't tell a virtual output from a sound card. WirePlumber reads it only on role-based stream loopbacks.
- **`monitor.channel-volumes = true`**: a virtual output's volume in the desktop's mixer then applies to what a session captures from its monitor. Otherwise that slider would do nothing, because a null sink plays nowhere.
- **No `priority.session`**: WirePlumber then ranks the device at 0, so it never becomes the default by itself. The user can still choose it as the default.
- **Every entry carries `flags = [ nofail ]`**, so that a bad entry costs one device and never the user's whole audio (finding 6).
- **A disabled device stays in its file** behind a condition that no context satisfies, so it keeps its id and name. The property is ours and set by no one. Setting it in a context drop-in would show all disabled devices, which is useful only for debugging.

**Changes take effect at once, without a restart.** The engine keeps the live graph in step with the files.

| Action | Order |
|---|---|
| Create, or enable | Write the file, then create a live node (the entry's `args` plus `object.linger = true`) through the `adapter` factory. |
| Rename | Write the file, then **create the new node before destroying the old one** (finding 4). For the moment in between, two nodes share the name. The engine must tolerate that, and follow the newer one (higher `object.serial`). |
| Disable, or delete | Write the file, then destroy the live node. Its apps move to the default device, which is what disabling means. |

- **Before creating, the engine looks for a node with that name**, because PipeWire won't refuse a duplicate.
- **On `RegistryMirror::ready`**, both at start and after a reconnect, the engine reconciles:
  - each enabled entry with no node gets one;
  - each marked node with no enabled entry in a file we could read is destroyed.
  - If a scope's file can't be read, that scope's nodes are left alone.
- **When PipeWire restarts**, the lingering live nodes go and the drop-ins bring the same devices back under the same names.

**The registry mirror** treats a node carrying `reverblabr.virtual-device` as a device, never as ours. That check comes before the name-prefix and client checks in `looksLikeOurs`, so a device shows in the inventory whatever its name.

**All users.** The engine may not use KDE Frameworks and the app doesn't run as root, so writing the system file goes through a small privileged helper:
- It is built with **KAuth** and exposes the action `io.github.reverblabr.virtualdevices.write`, `auth_admin_keep`. The desktop's polkit agent shows the password prompt.
- **It is called by the app, not the engine.**
- **It receives a structured list of devices, never file text.** It validates the list with the same code the engine uses to generate the file, and atomically writes exactly one path, the system file above, or removes it when the list is empty.
- **The engine reads the system file**, which is world-readable, to list the all-users devices. It makes the live changes in the current user's session itself, which needs no root.
- **Other users' sessions** pick up a change at their next PipeWire start.
- **Any change to an all-users device** (rename, enable, disable, delete) goes through the helper, and so through the prompt.

**What this changes in ADR-0002.** Reverblabr writes **PipeWire** drop-ins, and only the ones above, only when the user asks for a virtual device. It never writes WirePlumber configuration or scripts. Everything else in the app works with no such files, as before.

## Consequences

- Devices outlive the app and survive PipeWire restarts and reboots, and they appear in every audio tool under the user's name for them.
- **Renaming never loses an app's routing**, and doesn't sound on the real device in between.
- A bad entry costs one device, never the user's sound. A broken file costs the devices in it, not the daemon.
- The engine now creates lingering objects, which ADR-0002's "our links die with our process" doesn't cover. Only devices linger. Links and filter nodes keep `object.linger = false`.
- The engine owns more state than before:
  - two files to parse and write atomically;
  - a reconcile step;
  - a tolerated duplicate name during a rename.

  The link manager's and inventory's lookups by `node.name` must pick one node when two share it (the newer).
- A **system-wide device can't be switched off for one user alone**: disabling it goes through the helper and applies to everyone. Destroying it only in one session would last until that session's next PipeWire start.
- **Packaging** gains a KAuth helper, a polkit action and a D-Bus system service file, all installed under system paths. A development build must install them before the root prompt can be tried on the desktop. `tools/deps` gains KAuth and polkit.
- **Uninstalling Reverblabr leaves the drop-ins behind**, and PipeWire keeps creating the devices. Deleting the devices in the app, or the files by hand, removes them. The user guide says so.
- A Flatpak build would get only the permissions the portal grants on PipeWire objects and couldn't write `~/.config/pipewire`. Virtual devices there need their own decision, if Flatpak ever becomes a target.

## Alternatives considered

- **Live nodes only, recreated by the app at every start.** No files, but the devices vanish whenever Reverblabr isn't running, and apps routed to them lose their device. Rejected.
- **Files only, applied at the next PipeWire restart.** Simple, but nothing happens when the user clicks, and restarting PipeWire interrupts every app's sound.
- **Our own store (JSON) with the drop-in generated from it.** It would need a second file per scope, which the helper would have to write too, and the two could disagree. The drop-in with conditions holds everything, including disabled devices.
- **`pactl load-module module-null-sink` or `pw-loopback`.** A `pactl` null sink is an object of `pipewire-pulse`'s, and a loopback is a process of its own. Both are gone when PipeWire restarts, and they add nothing the adapter factory doesn't give directly.
- **Removing a disabled device from the file** and keeping it only in the app's memory or settings. It would lose the device's id, and with it the sessions and routes that use its name. It would also need that second store.
- **WirePlumber configuration** (a Lua script or `wireplumber.conf.d` rules creating the nodes). It works only while WirePlumber runs and puts policy configuration under our control, which ADR-0002 rules out.
- **`pkexec` running a shell command** for the all-users file. A general root shell for one file, with no validation and no desktop-integrated action; KAuth gives a narrow, named action.

<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Spike: virtual devices, the evidence

Measurements behind [ADR-0013](../adr/0013-virtual-devices.md). The virtual devices manager needs devices that exist whether or not Reverblabr runs, can be switched off without being forgotten, and can be renamed without disturbing the apps playing into them. This spike checks, before any code relies on them, the PipeWire and WirePlumber facts that design takes for granted.

- **Versions:** PipeWire 1.6.9, WirePlumber 0.5.17, Fedora 44
- **Where:** a private PipeWire daemon on its own runtime directory and D-Bus session bus, never in a real session. It is started the way [`tests/pw/harness/run-private-pipewire.sh`](../../tests/pw/harness/run-private-pipewire.sh) starts one, with either the harness's configuration directory (`PIPEWIRE_CONFIG_DIR` = the distribution's `pipewire.conf` plus the harness's test devices) or, for §5, no `PIPEWIRE_CONFIG_DIR` and a scratch `XDG_CONFIG_HOME`.
- **WirePlumber:** a policy-only profile, with no hardware monitors. The harness's own profile `reverblabr-test` is `[ policy, mixin.systemwide-session, mixin.stateless ]`. **The stateless mixin switches off stream state** (`hooks.stream.state`), which a desktop session has. So §4 ran on `[ policy, mixin.systemwide-session ]`, which keeps it. The desktop's `main` profile adds only the hardware monitors on top of that. §4's rename result was the same under the stateless profile too.
- **Nothing here is in the repository's code or tests.** The probes were throwaway. The facts the engine will depend on become integration tests with the drop-in writer and the device manager.

The one C probe used for §2–§4 creates a node the way the engine will (excerpt):

```c
struct pw_properties *p = pw_properties_new(
    "factory.name", "support.null-audio-sink",
    PW_KEY_NODE_NAME, name, PW_KEY_NODE_DESCRIPTION, description,
    PW_KEY_MEDIA_CLASS, mediaClass,            // Audio/Sink or Audio/Source/Virtual
    "audio.position", position,                // "FL,FR" or "MONO"
    PW_KEY_OBJECT_LINGER, linger,              // "true" or "false"
    "reverblabr.virtual-device", "true",
    "monitor.channel-volumes", "true", NULL);
struct pw_proxy *proxy = pw_core_create_object(core, "adapter",
    PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &p->dict, 0);
// wait for the proxy's bound event and a core sync, then disconnect
```

It destroys with `pw_registry_destroy(registry, globalId)` from a fresh connection, followed by a core sync. Anything it reports is read back with `pw-dump`, `pw-cli ls Node` and `pw-cli info <id>`.

## 1. A disabled entry in a drop-in

**Question.** Is a `context.objects` entry skipped when its `condition` matches no context property, while its neighbours are created?

A drop-in in the private configuration directory, `pipewire.conf.d/90-reverblabr-virtual-devices.conf`:

```
context.objects = [
    { factory = adapter
      args = { factory.name = support.null-audio-sink
               node.name = "reverblabr.virtual.aaaa0001"
               node.description = "Enabled output"
               media.class = Audio/Sink
               audio.position = [ FL FR ]
               monitor.channel-volumes = true
               reverblabr.virtual-device = true } }
    { factory = adapter
      args = { … node.name = "reverblabr.virtual.aaaa0002" node.description = "Disabled output" … }
      condition = [ { reverblabr.virtual-device.enabled = true } ] }
    { factory = adapter
      args = { … node.name = "reverblabr.virtual.aaaa0003"
               node.description = "Enabled mic \"quoted\" ünï"
               media.class = Audio/Source/Virtual
               audio.position = [ MONO ] … } }
    { factory = adapter
      args = { … node.name = "control.condition-true" media.class = Audio/Sink }
      condition = [ { core.daemon = true } ] }
]
```

**Result.** `pw-dump` listed `aaaa0001`, `aaaa0003` and `control.condition-true`, and **not `aaaa0002`**. So:
- a condition naming a property that the context doesn't have is false, and the entry is skipped;
- a condition on a property the daemon does have (`core.daemon = true`) is honoured.

This matches `man pipewire.conf`: "The object is created only if one of the expressions in the array matches to a context property."

Along the way:
- **Escaped quotes and non-ASCII** in `node.description` came through intact: `'Enabled mic "quoted" ünï'`.
- **The marker property** is kept on the node: `reverblabr.virtual-device = "true"`.
- **Ports.** With WirePlumber running, the `Audio/Sink` got `playback_FL/FR` inputs and `monitor_FL/FR` outputs, flagged `port.monitor = true`. The `Audio/Source/Virtual` got an `input_MONO` input and a `capture_MONO` output, also flagged as a monitor, as ADR-0007 already records for virtual sources.
- **`node.virtual` is not set** by `support.null-audio-sink`. It isn't set on a `pactl load-module module-null-sink` device either.
- **No priority is set.** WirePlumber's session priority falls back to 0 (`scripts/lib/node-utils.lua:11-17`), so with a test sink at `priority.session = 2000` present, the defaults stayed on the test devices.

## 2. Live creation

**Question.** What does `pw_core_create_object(core, "adapter", …)` with `object.linger = true` produce? Does the node outlive the client that made it, and what does it say about who made it?

**Result:**
- **With `object.linger = true`**, the node (global id 80) appeared with its ports while the probe was connected, and was **still there, unchanged, after the probe disconnected**.
- **With `object.linger = false`**, the node was gone as soon as its creator disconnected.
- **The node names no client.** Neither the global properties nor the node's info carry `client.id` or `application.name`, even while the creator is connected, and even though that client called itself `Reverblabr`. The global's properties are only:

  ```
  factory.id = "19"  node.description = "Live output"
  node.name = "reverblabr.virtual.bbbb0001"  media.class = "Audio/Sink"
  ```

  The info adds `object.linger = "true"`, `reverblabr.virtual-device = "true"`, `object.serial` and the factory's name.
- **The defaults didn't move** when the node appeared.

So the registry mirror's client-based checks (`looksLikeOurs` and `fillFromClient`, `engine/pw/registrymirror.cpp`) will never mark such a node as ours. Only the node-name prefix `reverblabr.` would do that.

## 3. Destroying

**Question.** Can a later client destroy a lingering node made by an earlier one, and a node the daemon made from a drop-in?

**Result.** Both went, with no error from the core:

```
lingering node id 93 (creator gone), drop-in node id 37
destroy 93: core error 0 (none)   -> lingering now: none
destroy 37: core error 0 (none)   -> drop-in now: none
```

A client outside a sandbox gets the permissions `r w x m` on every object (`pw-dump` of the clients). A client in a Flatpak sandbox gets what the portal grants instead; that is outside this spike.

**PipeWire does not refuse a second node with a name already in use.** A live node created with the same `node.name` as the drop-in's `aaaa0003` sat next to it, with another id. The engine must look before it creates, or two devices with one name appear.

## 4. Renaming

A node's properties can't be changed from outside: the node interface has `set_param` and `send_command`, but nothing that updates properties (`pipewire/node.h`). So a rename has to create the node again, with the same `node.name` and the new `node.description`.

**Question.** What does WirePlumber do to the streams that were playing into the node?

**Setup.** A lingering `reverblabr.virtual.dddd0001` "Old name" `Audio/Sink`, with two looping `pw-cat` streams:
- **S1** names it as its target in its own properties: `pw-cat -p --target reverblabr.virtual.dddd0001`, the way an app configured with a device does.
- **S2** started on the default sink and was moved with `pw-metadata <S2> target.object reverblabr.virtual.dddd0001`, the way a volume applet or `pactl move-sink-input` moves a stream.

`pw-dump -m` recorded the graph during the rename. The count is of links created into the default sink `rbtest.sink.stereo` while it ran.

| Order | Links into the default sink during the rename (3 runs) | Afterwards |
|---|---|---|
| destroy the old node, then create the new one | **6, 4, 6** | S1 and S2 both on the new node |
| create the new node, then destroy the old one | **0, 0, 0** | S1 and S2 both on the new node |

**Destroying first** makes WirePlumber move both streams to the default sink until the new node appears. In a real session that is the sound card, so the rename would be *heard*. When the new node appears, both come back: S1 through its property, S2 through the metadata target, which survives the gap (`pw-metadata` still shows `target.object = 'reverblabr.virtual.dddd0001'` for S2). The relink took 0.02 s.

**Creating first** leaves no gap. When the old node goes, WirePlumber finds another node with the name the streams target and moves them straight to it. The result was the same under the stateless profile (0 links into the default sink).

A **new** stream of S2's application did not go to the virtual node, before the rename or after. WirePlumber's stream state stored the application's volume and channels (`stream-properties` in its state directory), but not a target, for a move made through metadata. So nothing to do with node names is remembered for a new stream, and a rename can't break it. A user who wants every app on the device makes it the default device, which WirePlumber stores by `node.name`.

## 5. Drop-in precedence

**Question.** PipeWire reads drop-ins from `/usr/share/pipewire/pipewire.conf.d/`, `/etc/pipewire/pipewire.conf.d/` and `$XDG_CONFIG_HOME/pipewire/pipewire.conf.d/`. Does a file in a later directory replace a same-named file in an earlier one?

**Setup.** The daemon ran with no `PIPEWIRE_CONFIG_DIR` and a scratch `XDG_CONFIG_HOME`, with `PIPEWIRE_DEBUG=pw.conf:4`. The machine has `/usr/share/pipewire/pipewire.conf.d/50-raop.conf` and no `/etc/pipewire`. The user directory got its own `50-raop.conf`, holding one null sink, and a `00-noraop.conf` setting `module.raop = false` so that network discovery stayed off.

**Result.** **Same-named files do not replace each other.** Both were loaded, the system's first:

```
loaded config '/usr/share/pipewire/pipewire.conf.d/50-raop.conf' with 1 items
loaded config '<scratch>/pipewire/pipewire.conf.d/00-noraop.conf' with 1 items
loaded config '<scratch>/pipewire/pipewire.conf.d/50-raop.conf' with 1 items
```

An earlier run without `00-noraop.conf` showed the same thing from the outside: the user's null sink was created *and* the system file's `libpipewire-module-raop-discover` was loaded. So files are read directory by directory, system before user, and every one of them is used. `/etc` wasn't available to test without root. The manual's lookup order puts it between the two.

The original plan gave "a user file with the same name would override the system one" as the reason for two file names. **That reason is wrong for PipeWire 1.6.9.** The two names are still right, for a different reason: each file then has exactly one owner, the user or the helper, and an older or newer PipeWire that did mask same-named files couldn't hide one scope behind the other. Since both files always load, a device appearing in both would be created twice. Its id must be unique across the two.

## 6. What a broken file does

Two checks with a third entry in the drop-in:

| The file | Result |
|---|---|
| An entry with a factory that doesn't exist, **without** `flags = [ nofail ]` | **The daemon does not start**: `can't create object from factory adapter: No such file or directory`, then `failed to create context`. No PipeWire means no sound at all for the user. |
| The same entry **with** `flags = [ nofail ]` | The daemon runs; the bad entry is logged and skipped, and the good one is created. |
| A file cut off in the middle (`Mismatched bracket`) | The daemon runs, and **the whole file is ignored**, including its good entries. |

So every entry we write carries `flags = [ nofail ]`. Writes must be atomic so that a file is never cut off.

## How to reproduce

The launcher and probe were throwaway, but the facts can be checked with stock tools in the harness:

```sh
tests/pw/harness/run-private-pipewire.sh bash
# §2 and §3: a lingering node from one client, destroyed by another
pw-cli create-node adapter '{ factory.name = support.null-audio-sink node.name = "rb.probe" media.class = Audio/Sink object.linger = true }'
pw-cli ls Node | grep -A8 rb.probe          # no client.id
pw-cli destroy <id>
# §4: a stream targeting the node, then create-before-destroy
pw-cat -p --target rb.probe some.wav &
```

§1, §5 and §6 need a daemon started with a drop-in of your own. Copy `/usr/share/pipewire/pipewire.conf` into a directory, add a `pipewire.conf.d/`, and start `pipewire` with `PIPEWIRE_CONFIG_DIR` pointing at it, as the harness does. For §5, leave `PIPEWIRE_CONFIG_DIR` unset, point `XDG_CONFIG_HOME` at a scratch directory, and set `PIPEWIRE_DEBUG=pw.conf:4`.

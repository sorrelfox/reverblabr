<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Spike S1: capture and linking, the evidence

Measurements behind [ADR-0007](../adr/0007-capture-and-linking.md). Everything ran in the private PipeWire instance of the test harness ([`tests/pw/`](../../tests/pw/README.md)), never in a real session.

- **Versions:** PipeWire 1.6.8, WirePlumber 0.5.17, Plasma 6.7.5, Fedora 44
- **Test signal:** a 1 kHz sine at −12.00 dBFS peak, stereo (`FakeApp`)
- **Capture:** `rb::pwtest::CaptureNode`, a `pw_filter` with one input port per channel and a peak meter per port. Levels are read after they settle, because `wpctl` changes reach the audio a few cycles after the command returns.

To reproduce:

```sh
ctest --preset debug -R pw.capture-semantics --output-on-failure
tests/pw/harness/run-private-pipewire.sh bash   # a shell to explore by hand
```

The prototype that produced these numbers, `spikes/s1-capture/`, was deleted once the link manager had landed. `pw.capture-semantics` checks the same facts, one test per finding below.

## 1. Fan-out

`CapturingAStreamLeavesItPlayingToItsSink`: the stream's ports were linked by channel into the capture node. Both the capture node and the sink's monitor read −12.00 dBFS, and the stream's links to the sink stayed in place.

## 2. When to link

**The node comes first.** The prototype prints the number of ports it knows for a matching stream at the moment the node appears. Over 21 streams (20 restarts) it was always 0:

```
node id=85 serial=386 ports-known=0
port id=91 node=85 channel=FL
link stream=386 port=91 channel=FL -> 80
port id=90 node=85 channel=FR
link stream=386 port=90 channel=FR -> 81
```

**Moves re-create ports.** Output ports of one `pw-cat` stream, moved with `pw-metadata -n default <id> target.object <sink>`:

```
stereo sink: [{"id":80,"channel":"FL","serial":81},{"id":81,"channel":"FR","serial":82}]
5.1 sink:    [{"id":80,"channel":"FR","serial":89},{"id":81,"channel":"FL","serial":88},
              {"id":82,"channel":"LFE","serial":91},{"id":83,"channel":"FC","serial":90},
              {"id":84,"channel":"RL","serial":92},{"id":85,"channel":"RR","serial":93}]
mono sink:   [{"id":85,"channel":"MONO","serial":102}]
```

Port 80 was FL and came back as FR, with a new serial. Links to the old ports vanish with them (`MovingAStreamToAnotherLayoutRecreatesItsPorts`). The prototype re-linked after every move (`FollowsAStreamMovedAcrossLayouts`):

```
port id=86 node=84 channel=FL        # was FR before the move to 5.1
link stream=86 port=86 channel=FL -> 80
port id=89 node=84 channel=MONO      # was RR before the move to mono
link stream=86 port=89 channel=MONO -> 80
link stream=86 port=89 channel=MONO -> 81
```

**Restarts.** A restarted app is a new node, and it may get the old node id at once (found while building the harness). The prototype re-captured `TestApp` across 20 restarts: 21 streams, 42 links, no misses (`RecapturesTheAppAcross20Restarts`).

## 3. Volumes

`wpctl set-volume` works on a cubic scale: 0.5 is 60·log10(0.5) = −18.06 dB.

| Change | At the stream's output ports | At the sink's monitor ports |
|---|---|---|
| none | −12.00 | −12.00 |
| stream volume 0.5 | −30.06 | −30.06 |
| stream muted | silence | silence |
| sink volume 0.5 and muted | −12.00 | −12.00 |

A sink applies its volume to the monitor only with `monitor.channel-volumes = true`. That is off by default and unset on the ALSA sinks of the test machine. WirePlumber's fallback sink turns it on (`scripts/fallback-sink.lua`). With it forced on, `SinkVolumeIsNotAppliedToItsMonitorPorts` fails, as it should.

## 4. Channel layouts

The same stereo app, played to each test sink and captured by channel:

| Sink | Stream's output ports | Levels |
|---|---|---|
| stereo | FL FR | −12.00 each |
| 5.1 | FL FR FC LFE RL RR | FL, FR −12.00; FC, LFE, RL, RR silent (no upmix) |
| mono | MONO | −12.00 (average of identical channels) |
| 4 × AUX | FL FR | −12.00 each; the stream keeps its layout |

## 5. Summing

`LinksIntoOnePortAreSummedAtUnityGain`: the stream's FL and FR (the same signal) were both linked into one input port. It read −5.98 dBFS, that is −12.00 + 6.02 dB.

## 6. Alert sounds

KNotification plays notification sounds through libcanberra. Per sound it sets `event.id`, the sound theme, a fallback file and `canberra.cache-control = volatile`, and on its context the application's name, desktop id and icon; it sets no `media.role`. [`harness/play-notification.py`](../../tests/pw/harness/play-notification.py) does the same through libcanberra's PulseAudio backend. The resulting stream:

```json
{
  "media.class": "Stream/Output/Audio",
  "media.role": "Notification",
  "media.name": "message-new-instant",
  "event.id": "message-new-instant",
  "application.name": "Test Notifier",
  "application.id": "org.example.notifier",
  "application.icon-name": "dialog-information",
  "client.api": "pipewire-pulse",
  "node.name": "Test Notifier"
}
```

libcanberra asks for the PulseAudio role `event`, and pipewire-pulse turns it into `Notification`.

## 7. Re-played streams

Nodes that carry `node.link-group` with a loopback (`pw-loopback`), a filter chain ([`harness/filter-chain-copy.conf`](../../tests/pw/harness/filter-chain-copy.conf)), a combined sink (`module-combine-sink`) and an echo canceller (`module-echo-cancel`) loaded (the filter chain from a second run):

```
{"node.name":"rbtest.vsink.replay","media.class":"Stream/Output/Audio","node.link-group":"loopback-285626-20"}
{"node.name":"rbtest.vsink","media.class":"Audio/Sink","node.link-group":"loopback-285626-20"}
{"node.name":"rbtest.fc.sink","media.class":"Audio/Sink","node.link-group":"filter-chain-286325-7"}
{"node.name":"rbtest.fc.replay","media.class":"Stream/Output/Audio","node.link-group":"filter-chain-286325-7"}
{"node.name":"rbtest.combined","media.class":"Audio/Sink","node.link-group":"combine-sink-285514-13"}
{"node.name":"output.rbtest.combined_rbtest.sink.stereo","media.class":"Stream/Output/Audio","node.link-group":"combine-sink-285514-13"}
{"node.name":"output.rbtest.combined_rbtest.sink.mono","media.class":"Stream/Output/Audio","node.link-group":"combine-sink-285514-13"}
{"node.name":"echo-cancel-capture","media.class":"Stream/Input/Audio","node.link-group":"echo-cancel-285514-14"}
{"node.name":"rbtest.ec.source","media.class":"Audio/Source","node.link-group":"echo-cancel-285514-14"}
{"node.name":"rbtest.ec.sink","media.class":"Audio/Sink","node.link-group":"echo-cancel-285514-14"}
{"node.name":"echo-cancel-playback","media.class":"Stream/Output/Audio","node.link-group":"echo-cancel-285514-14"}
```

The loopback, combined-sink and echo-canceller nodes are also `node.virtual = true`. The application's stream carries neither property. WirePlumber uses `node.link-group` the same way, to recognise a filter and walk through it (`autoswitch-bluetooth-profile.lua`). Not tried: effect applications that build their own nodes instead of using these modules.

## 8. Feedback

`pw-loopback` captured the stereo sink's monitor and played back into the same sink. PipeWire linked both sides without complaint, and kept both links after the only real source stopped: nothing in the server refuses or breaks a cycle.

What the circulating sound does then depends on the machine. On the development machine the sink's monitor still read −11.97 dBFS a second after the source stopped, and −12.00 in later runs: with a gain of 1 around the loop, the audio keeps going. In a CI container the same run decayed to −84 dBFS. The test therefore requires the links to survive, and prints the level rather than demanding it.

Two sessions capturing each other's output form the same kind of loop.

## 9. Node identity

**PulseAudio clients.** `pactl list source-outputs` is what Plasma's microphone indicator lists: plasma-pa's `MicrophoneIndicator` is built on PulseAudioQt's `SourceOutputModel`. pavucontrol shows the same list. With a capture node of class `Stream/Input/Audio` linked to the stereo source and a `pw-record` stream running, only `pw-record` was listed. Nodes of class `""`, `Audio/Sink` and `Audio/Duplex` were not listed as sinks or sources either. The likely reason: a `pw_filter` has no format on the node itself, and pipewire-pulse lists only nodes it can describe with a sample format.

**The policy.** With `node.autoconnect = false`, WirePlumber linked nothing to a capture node, whether its class was unset or `Stream/Input/Audio`.

**Bluetooth.** No headset was available, so this answer comes from WirePlumber 0.5.17's `scripts/device/autoswitch-bluetooth-profile.lua`:
- a Bluetooth device always has a loopback source node (`bluez5.loopback = true`), in every profile
- when a link is added, the script looks at the linked node. Only a `Stream/Input/Audio` node without `node.link-group`, not a monitor, starts an evaluation.
- it switches to the headset profile if the loopback source is running and such a stream is linked to it, directly or through filters found by `node.link-group`; otherwise it restores the previous profile

So a capture node of any other class gets silence from a headset that is in its playback profile. The chosen design has yet to be confirmed on real hardware, with the Input Device block.

## 10. Clean-up

`ItsLinksDieWithIt`: the prototype's links are created with `object.linger = false`. After `kill -9` its node and every link to it disappeared, and the app went on playing to its sink at −12.00 dBFS. Links made with `pw-link`, which the tests use, linger by default and stay after `pw-link` exits.

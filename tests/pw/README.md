<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# PipeWire integration tests

These tests run against a **private PipeWire instance** that can't touch the developer's real audio session: it has its own devices, its own policy daemon and its own D-Bus. The same setup runs locally and in CI.

```sh
ctest --preset debug -R '^pw\.'                                                   # through CTest
tests/pw/harness/run-private-pipewire.sh build/debug/bin/reverblabr-pw-harness-test  # directly
tests/pw/harness/run-private-pipewire.sh bash                                     # a shell inside it
```

The shell is handy for exploring: `pw-dump`, `pw-cli ls Node`, `wpctl status` and `pw-cat` all talk to the private instance there.

## What the harness sets up

[`harness/run-private-pipewire.sh`](harness/run-private-pipewire.sh) starts `pipewire`, `wireplumber` and `pipewire-pulse`, runs the command, then stops everything and deletes its files.

| Isolation | How |
|---|---|
| Runtime directory | A fresh `/tmp/rbpw.XXXXXX`, used as `XDG_RUNTIME_DIR` and `PIPEWIRE_RUNTIME_DIR`. It must be a short path, because Unix socket paths are limited to 108 bytes. |
| Configuration and state | Fresh `XDG_CONFIG_HOME` and `XDG_STATE_HOME`, so the user's settings don't apply and their saved state isn't changed |
| PipeWire config | The distribution's `pipewire.conf` plus [`pipewire.conf.d/`](harness/pipewire.conf.d/). `PIPEWIRE_CONFIG_DIR` (set for the daemon only) keeps the system drop-ins out; one of them may enable discovery of network speakers. |
| Policy | WirePlumber's `policy` profile ([`wireplumber.conf.d/`](harness/wireplumber.conf.d/)). It has no hardware monitors, so no sound card or Bluetooth device is opened. It keeps no state, and has no portals, logind or device reservation. |
| D-Bus | A private session bus (`dbus-run-session`); `GIO_USE_VFS=local` stops GLib from starting GVFS on it |

**Test devices.** Every device is a null device whose name starts with `rbtest.`. The sinks leave `monitor.channel-volumes` at its default (off), as ALSA sinks do, so a sink's monitor carries what reaches it before the sink's own volume:

| Node | Class | Channels |
|---|---|---|
| `rbtest.sink.stereo` (default sink) | `Audio/Sink` | FL FR |
| `rbtest.sink.mono` | `Audio/Sink` | MONO |
| `rbtest.sink.surround51` | `Audio/Sink` | FL FR FC LFE RL RR |
| `rbtest.sink.aux4` | `Audio/Sink` | AUX0–AUX3 |
| `rbtest.source.stereo` (default source) | `Audio/Source/Virtual` | FL FR |
| `rbtest.source.mono` | `Audio/Source/Virtual` | MONO |

**Variables the command sees:**
- `RB_PW_HARNESS=1`. The tests refuse to run without it.
- `RB_PW_HARNESS_DIR`: this run's directory. Logs go to `logs/`, and the test tone is `tone.wav`.
- `RB_PW_HARNESS_PULSE`: `1` when `pipewire-pulse` is running. Pass `--no-pulse` to skip it.
- `RB_HOST_XDG_RUNTIME_DIR`: the caller's real runtime directory, used by the isolation check.

When the command fails, the harness prints the tail of each daemon's log.

**Other files in [`harness/`](harness/):**
- `play-notification.py` plays a sound through libcanberra with the properties KDE notifications use. It needs no display.
- `filter-chain-copy.conf` is a minimal filter chain (a virtual sink re-played as a stream), started with `pipewire -c`.

## Test programs

| CTest | Binary | What it checks |
|---|---|---|
| `pw.harness` | `reverblabr-pw-harness-test` | the harness itself: devices, fake apps, moves, levels, isolation |
| `pw.dump-on-failure` | the same | a failing test prints a `pw-dump` |
| `pw.capture-semantics` | `reverblabr-pw-capture-test` | how capture and linking behave, one test per fact of [ADR-0007](../../docs/adr/0007-capture-and-linking.md) |
| `pw.registry` | `reverblabr-pw-registry-test` | the engine's connection and registry mirror, judged against `pw-dump` |
| `pw.links` | `reverblabr-pw-link-test` | the engine's link manager, including the feedback guard |
| `pw.session-node` | `reverblabr-pw-session-test` | the session processing node: its ports and properties, levels through a graph, the handover, Stop, the load and xrun figures (including a block held past its deadline in one cycle of eight, which the count has to show, and a change of driver, which it must not); and editing a session while it runs: a thousand edits under a tone, lanes coming and going, changes of quantum and of rate |
| `pw.inventory` | `reverblabr-pw-session-test --gtest_filter=Inventories.*` | the lists the pickers read: the harness's devices and their channel layouts, an application and a device appearing and going within the quarter second, two streams of one application, our own session node left out of every list, and the default following the policy |
| `pw.capture` | `reverblabr-pw-session-test --gtest_filter=Capture.*` | capturing one application: the tone arrives while the application goes on playing to its own device, twenty restarts are each captured again within 250 ms, two streams sum without touching each other, a stream moved to another sink keeps being captured, and a surround stream grows a port per channel |
| `pw.system` | `reverblabr-pw-session-test --gtest_filter=SystemAudio.*` | System Audio: three applications on three frequencies all arrive whole, an alert is left out until it is asked for, an exclusion takes effect within 250 ms and can be taken off again, a stream re-played through a combined sink is not counted twice, and two sessions capturing everything into one device stay bounded |
| `pw.devices` | `reverblabr-pw-session-test --gtest_filter=Devices.*` | the device blocks: the session is heard on the device it names, half the volume is −6.02 dB, following the default moves within 250 ms and leaves nothing behind, a stereo session on a 5.1 device uses the front pair only, a device unplugged and plugged in again is found, two devices play at once; and on the way in, a tone through a virtual microphone, a mono device on both sides at its own level, a four-channel device's channels picked by name, a microphone replugged within 500 ms, the default input followed, and a session that cannot record itself |
| `pw.scenarios` | `reverblabr-pw-session-test --gtest_filter=Scenarios.*` | the sessions M1 is judged by, built through the facade from the main blocks only: a microphone through Peak/RMS, Volume, Dynamics and Peak/RMS again to a device; an application at 200 % through Dynamics; System Audio with one application left out beside a microphone; and three applications and two microphones, one of them mono, merging into one tail. Each lane plays its own frequency so every source can be found at the far end, the meters are read from the telemetry snapshot the interface reads and held against what the device received, and the compressor against the curve the editor draws. Then the budget: that five-lane session at 48 kHz / 256 must cost this process under 3 % of one core, judged in the release build only |

### Long runs

`pw.session-node` ends with a soak that watches for xruns. It runs for `RB_SOAK_SECONDS` seconds, **ten by default**: two thousand cycles at quantum 256, which is enough to catch a node that has stopped running or one that loses a cycle whenever something happens, and short enough that a full run of the suite doesn't spend a minute listening to a tone that is fine. The ten minutes the session node is judged over are:

```
RB_SOAK_SECONDS=600 tests/pw/harness/run-private-pipewire.sh \
    build/debug/bin/reverblabr-pw-session-test --gtest_filter='SessionNodes.TheLoadAndThe*'
```

The two tests that listen for a feedback ring, `Links.TwoSessionsOnTheSameSinkStayBounded` and `SystemAudio.TwoSessionsCapturingEverythingStayQuietTogether`, listen for the same `RB_SOAK_SECONDS`; the minute the feedback guard and System Audio were judged over is `RB_SOAK_SECONDS=60`.

### How long it takes

A full run of the suite is about three minutes, and nearly all of it is the tests that share the `pipewire-audio` lock, which run one after another. `ctest --output-on-failure` prints every test's time; before making one of the locked tests longer, look at what the chain already costs. The thousand edits run at a forced quantum of 128 rather than the private server's own, about 2048 frames, because each edit waits one cycle for the handover.

## Writing a test

Derive the fixture from `rb::pwtest::PipeWireTest` ([`pwtest.h`](pwtest.h)):

```cpp
class Capture : public rb::pwtest::PipeWireTest {};

TEST_F(Capture, SeesTheApp)
{
    rb::pwtest::FakeApp app; // "TestApp" playing a 1 kHz tone at -12 dBFS
    const auto stream = rb::pwtest::waitForNode(rb::pwtest::playbackStreamOf("TestApp"));
    ASSERT_TRUE(stream);
    // … exercise the engine, then check links and levels:
    EXPECT_NEAR(*rb::pwtest::measurePeakDb(rb::pwtest::kStereoSink), rb::pwtest::kTonePeakDb, 0.5);
}
```

- **The fixture:**
  - fails immediately outside the harness
  - prints a `pw-dump` of the private graph at a test's first failure
  - after each test, waits until no stream is left, so tests don't see each other's leftovers
- **`Graph::capture()`** takes a snapshot with `pw-dump`: nodes, ports, links and the `default` metadata. The harness watches the graph independently of Reverblabr's own PipeWire code, so it can judge that code.
- **`waitForNode`, `waitForPorts`, `waitForLink`, `waitForNodeGone` and `waitUntil`** poll with a 10-second limit. Never use a fixed sleep instead.
- **`FakeApp`** plays the tone as a native (`pw-cat`) or Pulse (`paplay`) client, with the `application.name`, `application.process.binary`, `media.role` and target you choose. `restart()` gives a new stream.
- **`moveStream()`** moves a stream to another sink, as a volume applet would.
- **`measurePeakDb()`** records a sink's monitor and returns the peak level.
- **`CaptureNode`** ([`capturenode.h`](capturenode.h)) is a filter node with one input port per channel and a peak meter on each. The policy never links it; the test links it with `linkByChannel()` or `linkPorts()`. `measure()` returns the peak per channel over a window, and `measureSettled()` repeats until the level is stable, for use after a volume change. With `sineHz` set, `measureSine()` also checks the ports carry a clean sine: any click, gap or repeated block shows as a residual above the float rounding of about −140 dBFS.
- **`linkPorts()`, `unlinkPorts()` and `linkByChannel()`** create links like `pw-link`. They linger until a port goes away.
- **`setVolume()` and `setMute()`** go through `wpctl`, as a volume applet would. The volume is cubic: 0.5 is −18 dB. The change reaches the audio a few cycles after the call returns.
- **`BackgroundProcess`** runs a helper such as `pw-loopback`, logs its stdout, and can `kill()` it with SIGKILL to simulate a crash. **`runCommand()`** runs a tool to completion.
- **`dumpHostSession()`** reads the real session, only to check that nothing leaked into it.

## Things the harness taught us

- **PipeWire reuses node ids.** A restarted application can come back with the same id straight away. Identify an object over time by its `object.serial`, which is never reused.
- **Virtual sources flag their outputs as monitors.** The output (`capture_*`) ports of an `Audio/Source/Virtual` node carry `port.monitor = true`, although they are the source's real outputs. Don't use that flag on its own to recognise a sink's monitor ports.
- **Moving a stream re-links it.** After `target.object` changes, the policy links the stream to the new sink, converting to its channel layout, and removes the old links. When the layout changes, the stream's ports are destroyed and re-created, and their ids come back with other channels.
- **Volume changes are asynchronous.** `wpctl` returns before the audio changes, so measure with `measureSettled()` or wait for the new value.

More findings, with measurements, are in [the capture spike notes](../../docs/dev/spike-s1-capture.md). The prototype they were taken from is gone; `pw.capture-semantics` is what keeps checking them.

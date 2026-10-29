<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Development

This page is for working on Reverblabr itself: setting up a machine, building and testing, the tools I use to look inside PipeWire, and how a release is made. If you only want to use Reverblabr, [Installing](installing.md) is enough.

## Platforms

Reverblabr targets **KDE Plasma on Wayland** only.

| Platform | Role |
|---|---|
| **Fedora 44 or newer, KDE Plasma** | Where development happens, and what gets tested the most |
| **Kubuntu 26.04 LTS** | Secondary. It ships the oldest libraries Reverblabr supports, so it sets the minimum versions below |

The build checks these minimum versions:

| Component | Minimum |
|---|---|
| CMake | 3.24 |
| C++ compiler | GCC 15 or Clang 21 (C++23) |
| Qt | 6.10 |
| KDE Frameworks | 6.24 |
| PipeWire | 1.6 |
| WirePlumber (at run time) | 0.5 |

Stay within the Qt 6.10 and KDE Frameworks 6.24 APIs. Fedora ships newer versions, so code that uses something newer builds fine there and then breaks on Kubuntu. Check the "since" note in the API documentation before using a recent function.

For reference, my machine and a Kubuntu install as of September 2026:

| Component | Fedora 44 KDE | Kubuntu 26.04 |
|---|---|---|
| Qt | 6.11.2 | 6.10.2 |
| KDE Frameworks | 6.30 | 6.24 |
| PipeWire | 1.6.8 | 1.6.2 |
| WirePlumber | 0.5.17 | 0.5.13 |
| GCC / Clang | 16.2 / 22.1 | 15.2 / 21.1 |
| CMake | 4.3 | 4.2 |

## Setting up

Get the source and install the packages. The lists in [`tools/deps/`](../tools/deps/) have one package per line, and CI installs from the same files:

```sh
git clone --branch dev https://github.com/sorrelfox/reverblabr.git
cd reverblabr
sudo dnf install $(sed 's/#.*//' tools/deps/fedora.txt)        # Fedora
sudo apt install $(sed 's/#.*//' tools/deps/kubuntu.txt)       # Kubuntu 26.04
tools/check-deps.sh
```

`tools/check-deps.sh` prints each tool and library with the version it found, and fails if something required is missing or too old. Optional tools, such as the sanitizer runtimes or the PipeWire utilities, only produce warnings.

[What to install](dependencies.md) says what every package is for. A test (`tools.dependencies-documented`) fails when a list has a package that page doesn't mention, so a new dependency goes on the page in the same change.

Development happens on `dev`. `main` only receives releases.

### Git hooks

Reverblabr is an original work, and names or text from other products must not end up in it by accident. Two hooks in [`tools/hooks/`](../tools/hooks/) help with that: `pre-commit` rejects a commit when a staged file name or an added line contains a term from a local list, and `commit-msg` does the same for the message. Turn them on with:

```sh
git config core.hooksPath tools/hooks
```

The list lives in `.git/info/forbidden-terms`, one term per line, matched without regard to case; `#` starts a comment. It is never committed, and without it the hooks do nothing. The README is the one file they don't check, because it names the program that inspired this one.

## Building and testing

The build uses CMake presets:

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

| Preset | What it is for |
|---|---|
| `debug` | Everyday development |
| `release` | An optimised build |
| `asan` | AddressSanitizer and UndefinedBehaviorSanitizer |
| `tsan` | ThreadSanitizer, for the engine's threads |
| `rtsan` | Clang's real-time sanitizer, which catches allocations and locks on the audio path |

Each preset builds into `build/<preset>/`, and warnings are errors in all of them. There is one extra test preset, `rtsan-offline`, which runs the `rtsan` build's tests without the suites that start a private PipeWire (the ones labelled `pipewire`). `build/<preset>/compile_commands.json` is there for clangd and other editor tooling.

The tests are GoogleTest cases for each engine layer, Qt Quick tests for the QML interface, and a check that the build refuses forbidden dependencies between the layers ([architecture.md](architecture.md) describes the layers). `ctest --preset debug -R <regex>` runs a subset; test names start with their layer, for example `dsp.` or `qml.`.

### PipeWire integration tests

The tests that need a real audio graph run inside a private PipeWire instance with its own devices, policy daemon and D-Bus. They never touch your desktop's audio, and they run the same way in CI.

```sh
ctest --preset debug -R '^pw\.'                  # the integration tests
tests/pw/harness/run-private-pipewire.sh bash    # a shell inside the private instance
```

[tests/pw/README.md](../tests/pw/README.md) describes the setup, the test devices and the helpers for writing tests.

`RB_PW_NO_REALTIME=1` keeps PipeWire's real-time module out of Reverblabr's own connection, so its data loop never asks rtkit for real-time scheduling. It helps tell a problem real-time scheduling causes from one it doesn't. rtkit also limits how many requests one user can make in a burst, so on a busy machine some test processes get real-time and some don't. A test that depends on real-time asks the node (`SessionTelemetry::realtime`) instead of assuming it.

### Engine daemon tests

`bus.engine` (`tests/bus`) runs the window's `RemoteEngine`, the daemon's `EngineService` and `reverblabrd` itself on a private session bus: `dbus-run-session` with a configuration whose only service directory holds an activation file for the `reverblabrd` just built. Nothing is installed or enabled for systemd, your own session bus never sees it, and `PIPEWIRE_REMOTE` names a socket that doesn't exist, so no daemon it starts can reach your audio.

Where the system allows it, the test also runs in a PID namespace of its own (`unshare --user --pid`). It starts and kills daemons by pid, and a wrong pid is not a small mistake: `kill(0, …)` signals the whole process group and `kill(-1, …)` every process you own, which is your desktop session. Inside the namespace the test can only reach what it started. Where unprivileged namespaces are refused (Ubuntu's AppArmor default), `signalProcess()`, which refuses anything but an ordinary pid, is the remaining guard. Any test that signals a process by pid has to go through such a guard: `signalProcess()` in `tests/bus`, or the one in the PipeWire test kit (`tests/pw/pwtest.h`).

Which engine a window gets: the daemon's when the session bus can start it, otherwise its own, with a banner explaining why. A build tree that hasn't been installed has no activation file on your session bus, so `build/debug/bin/reverblabr` runs the engine in the window. `--in-process` skips asking the bus, and it's what to use while working on the engine itself, because an installed `reverblabrd` is otherwise what a build-tree window would reach, not the engine you just built. The `bus.reach.*` tests check that choice on four private buses.

To try the window on the daemon without installing anything, give both the same private bus and a scratch home:

```sh
S=$(mktemp -d); mkdir -p $S/{data,state,config,cache}
env HOME=$S XDG_DATA_HOME=$S/data XDG_STATE_HOME=$S/state XDG_CONFIG_HOME=$S/config XDG_CACHE_HOME=$S/cache \
    dbus-run-session --config-file=build/debug/tests/bus/private-bus/session.conf -- build/debug/bin/reverblabr --daemon
```

Set the variables before `dbus-run-session`: the daemon is started by that bus and inherits its environment, not the window's.

The telemetry tests watch the bus with `dbus-monitor` ([ADR-0018](adr/0018-telemetry-in-shared-memory.md)). While a mock daemon publishes sixty level snapshots a second and a window reads every one, `dbus-monitor --profile` on the private bus must see no message at all. The same suite counts the descriptors in `/proc/<pid>/fd` and the shared-memory mappings in `/proc/self/maps`, so a leak across windows or daemons fails it.

### Comparing with the design

`ctest --preset design` compares the window with the design mock-up it was drawn from, state by state, as a share of differing pixels, and writes a report with images of where they differ. The mock-up isn't part of the repository and the comparison needs Firefox, so without either the test skips itself. It isn't part of the other presets. [tests/design/README.md](../tests/design/README.md) has the details.

## Sanitizers

The sanitizer presets build everything with one sanitizer and run the whole test suite under it:

```sh
cmake --preset asan && cmake --build --preset asan && ctest --preset asan
```

| Preset | Catches | Runtime options (set by the test preset) |
|---|---|---|
| `asan` | Out-of-bounds access, use after free and leaks, plus undefined behaviour such as signed overflow or misaligned access | `ASAN_OPTIONS` turns on leak, stack-use-after-return and initialisation-order checks; `UBSAN_OPTIONS` stops at the first report; `PIPEWIRE_DLCLOSE=false` keeps PipeWire's modules loaded (see below) |
| `tsan` | Data races and lock-order problems | `TSAN_OPTIONS` loads [`tools/sanitizers/tsan.supp`](../tools/sanitizers/tsan.supp) |
| `rtsan` | Allocations, locks and system calls inside functions marked `RB_NONBLOCKING`. Needs Clang | `RTSAN_OPTIONS` stops at the first violation |

To run the app or a single test binary outside CTest, set the same options yourself:

```sh
TSAN_OPTIONS="suppressions=$PWD/tools/sanitizers/tsan.supp" build/tsan/bin/reverblabr
```

### Real-time safety

Every function that runs on the audio thread is marked `RB_NONBLOCKING`, from [`engine/rt/realtime.h`](../engine/rt/realtime.h). It goes after `noexcept`, on both the declaration and the definition:

```cpp
void process(ProcessContext &context) noexcept RB_NONBLOCKING;
```

- With Clang, the compiler checks every marked function (`-Wfunction-effects`, an error because warnings are errors). It rejects heap allocation, locks, and calls to anything it can't prove safe. Inline functions with visible bodies, math functions and other marked functions are fine, so mark the helpers a real-time function calls as well.
- The `rtsan` preset also checks at run time and stops a test at the first violation. `sanitizers.rtsan-reports-allocation` proves the sanitizer works: it expects a report from a deliberate allocation.
- With GCC, `RB_NONBLOCKING` does nothing. There, the allocation trap in the DSP test kit catches allocations on real-time paths.

### Suppressions

Qt, KDE Frameworks and libdbus come from the distribution and aren't built with ThreadSanitizer, so TSan reports races, lock-order warnings and thread leaks inside them that aren't real. [`tools/sanitizers/tsan.supp`](../tools/sanitizers/tsan.supp) suppresses reports whose stack runs through those libraries, and each entry says why. Never add Reverblabr's own code to it: a race in our code has to be fixed.

PipeWire unloads its modules (`dlclose`) when a client shuts down. Memory a module still refers to from its globals then looks leaked, so LeakSanitizer reports dozens of "leaks" with unknown frames under `pw_context_load_module`, even after a clean `pw_deinit()`. The `asan` test preset sets `PIPEWIRE_DLCLOSE=false`, which keeps the modules loaded: the false reports go away and real leaks still show. Set it too when you run a PipeWire client built with ASan by hand.

## Formatting and linting

- **C++:** `clang-format -i <files>` applies [`.clang-format`](../.clang-format), and `clang-tidy -p build/debug <file>` runs the checks in [`.clang-tidy`](../.clang-tidy).
- **QML:** `qmllint -I build/debug/bin <files>`. On Fedora the tool is `/usr/lib64/qt6/bin/qmllint`, also installed as `qmllint-qt6`; on Kubuntu it's `/usr/lib/qt6/bin/qmllint`.
- **Translatable strings:** `tools/lint-i18n.py app` flags user-visible strings that bypass KI18n, and `cmake --build --preset debug --target extract-messages` writes the template `po/reverblabr.pot`. See [i18n.md](i18n.md).
- **Editors** pick up indentation and line endings from [`.editorconfig`](../.editorconfig).

## Before a change goes in

CI builds and tests `main` and pull requests into it, but not `dev`, so on `dev` these checks are yours to run. This is the full list:

```sh
reuse lint
git ls-files '*.cpp' '*.h' | xargs clang-format --dry-run -Werror
cmake --build --preset debug   && ctest --preset debug
cmake --build --preset release && ctest --preset release
cmake --build --preset asan    && ctest --preset asan
cmake --build --preset rtsan   && ctest --preset rtsan-offline
cmake --build --preset debug --target all_qmllint   # a warning counts as a failure
```

Run each suite whole. `-R` is for narrowing things down while you work on one problem, not for the final check. A suite has passed when nothing failed and nothing was left out, apart from the skips it declares itself (such as the performance tests that refuse to report a timing measured without optimisation).

| Suite | What only it tells you |
|---|---|
| `debug` | Assertions, the layer rules and the QML tests. The build you develop in, and the cheapest place to find a mistake. |
| `release` | What the optimiser makes of code that is only nearly right. A `std::string_view` that outlived the value it pointed into once sent the wrong device channel through every optimised build, and only this suite failed. |
| `asan` | Memory read after it was freed, read past its end, or never given back. For a dangling view like the one above it names `stack-use-after-scope`, with the line that freed the memory and the line that read it. |
| `rtsan` | The only build with Clang, so the only one where Clang's own diagnostics become errors, including `-Wfunction-effects`. At run time it stops a test at the first allocation, lock or system call inside an `RB_NONBLOCKING` function. |

`release` and `asan` aren't extras for changes that look risky. A check you only run when you already suspect something doesn't catch the problems nobody suspected. Each suite takes around four minutes on my machine, and all of them together well under a quarter of an hour.

`rtsan-offline` leaves out the suites under `tests/pw` that start a private PipeWire, which keeps it short. When a change reaches the audio path (`engine/rt`, `engine/dsp`, `engine/blocks`, `engine/graph`, or the node and processor in `engine/pw`), run the whole `ctest --preset rtsan` on top: those suites are the ones where a real process callback runs on a real real-time thread. Add `ctest --preset tsan` when a change touches threads or state shared between them.

Leave the PipeWire suites out by label, not by name. `-E '^pw\.'` looks the same and isn't: the pw layer's own unit tests are called `pw.SessionProcessorTest.*`, `pw.VirtualDeviceConf.*` and so on. They need no daemon and include the tests of the code that runs in the process callback.

Read the build output before the test output. A target that fails to compile doesn't show up as a failing test: its tests are reported as "Not Run" underneath an otherwise clean result, or aren't listed at all. `cmake --build` has to succeed before a `ctest` result means anything.

## Running the app

```sh
build/debug/bin/reverblabr
```

Run it from a Plasma Wayland session. To see debug logging, enable the categories you need:

```sh
QT_LOGGING_RULES='reverblabr.*.debug=true' build/debug/bin/reverblabr
```

Only one Reverblabr runs at a time. Starting it again hands the new launch's options to the running one (`--new-session` opens a session there, `--sessions` the Sessions window, nothing at all brings its window forward), and the new process exits. So with an installed Reverblabr open, `build/debug/bin/reverblabr` only raises the installed one's window; quit that first. `--mock` is the exception: it opens a window of its own next to whatever else runs, with a mock engine and no PipeWire. `bus.launch` checks both.

### Trying the all-users helper

Virtual devices for all users go through a helper that the system bus starts as root, asked through KAuth, and polkit shows the administrator's password prompt. A build makes the helper when KF6Auth is found (`RB_BUILD_AUTH_HELPER`), but the prompt only appears once its four files are where D-Bus and polkit look for them, which only root can arrange. They are one install component, so nothing else of the build gets installed:

```sh
cmake --build --preset debug
sudo cmake --install build/debug --component authhelper --prefix /usr
```

That writes exactly these files:

| File | What it is |
|---|---|
| `/usr/libexec/kf6/kauth/reverblabr-virtualdevices-helper` | the helper, where KAuth's service file expects it |
| `/usr/share/dbus-1/system.d/io.github.reverblabr.virtualdevices.conf` | lets root own the helper's name on the system bus |
| `/usr/share/dbus-1/system-services/io.github.reverblabr.virtualdevices.service` | lets the system bus start it on demand |
| `/usr/share/polkit-1/actions/io.github.reverblabr.virtualdevices.policy` | the action and its prompt text |

`--prefix /usr` matters: the two D-Bus files follow the prefix, and D-Bus only reads `system.d` under `/usr/share` and `/etc`. The helper is built from the tree you installed it from, so install it again after changing `helper/`. To remove it:

```sh
sudo xargs rm -v < build/debug/install_manifest_authhelper.txt
```

The devices it wrote stay in `/etc/pipewire/pipewire.conf.d/90-reverblabr-virtual-devices-system.conf` until they are deleted in the app, or the file is.

## Looking inside PipeWire

Reverblabr's nodes are named `reverblabr.<session-id>.seg<N>`, and their description is `Reverblabr — <session name>`.

| Tool | What it's for |
|---|---|
| `qpwgraph` | A live view of nodes, ports and links: the quickest way to see what is connected to what |
| `pw-top` | DSP load, quantum, sample rate and xrun counts per node |
| `pw-dump` | The whole graph as JSON. Pipe it to `jq`, for example `pw-dump \| jq '.[] \| select(.info.props."node.name"? // "" \| startswith("reverblabr."))'` |
| `pw-cli` | `pw-cli ls Node`, `pw-cli info <id>`: list and inspect objects |
| `pw-metadata` | Graph settings. `pw-metadata -n settings 0 clock.force-quantum 256` forces a quantum, `clock.force-rate 44100` a rate, and `0` resets either |
| `wpctl` | `wpctl status` shows devices, streams and the defaults WirePlumber picked |
| `pw-play` | Plays a file as a stand-in application: `pw-play -P '{ application.name = "Test Player" }' tone.wav` |

## Checking a clean install in a container

[`tools/container-check.sh`](../tools/container-check.sh) copies the committed tree (`git archive HEAD`) into a fresh container, installs the package list, runs `tools/check-deps.sh` and configures the project:

```sh
tools/container-check.sh fedora:44              # install, check, configure
tools/container-check.sh fedora:44 test         # also build and run the tests
tools/container-check.sh ubuntu:26.04           # the secondary platform
```

It uses Podman; set `CONTAINER_ENGINE=docker` to use Docker instead. Only committed changes are checked.

## Making a release

The version is written in exactly one place, the first line of [`VERSION`](../VERSION) (`MAJOR.MINOR.PATCH`). CMake reads it into `project()`, and every program takes it from there through `version/buildversion.h` (`rb::version()`, `rb::versionFull()`): the window's About and `--version`, `reverblabrd --version`, `reverblabr-ctl --version`, `reverblabr-plugin-host --version`, and the engine's line in the diagnostics. Never write the version anywhere else; packaging files take it as `@VERSION@`.

Between releases a build says where it is: `0.1.0+42.gabc1234` is 42 commits after the tag `v0.1.0`, at commit `abc1234`; `0.1.0+gabc1234` is before that tag exists; `.dirty` means the tree had changes. `cmake/RbVersionDescribe.cmake` works this out from `git describe` on every build. A tree without git, such as a release tarball, just says `0.1.0`.

A release is two commands, with the release notes and the checks in between. They run on `dev`, and then `main` moves forward to the release:

```sh
tools/release.sh prepare 0.2.0   # writes VERSION and adds a dated 0.2.0 release to the AppStream data, with @NOTES@ to fill in
$EDITOR app/io.github.reverblabr.Reverblabr.metainfo.xml   # the notes, in place of @NOTES@
# the checks from "Before a change goes in"
tools/release.sh tag             # commits "release: 0.2.0" (signed off) and tags v0.2.0; never pushes
git switch main && git merge --ff-only dev && git switch dev
git push origin dev main v0.2.0
```

`prepare` refuses a tree with changes, a version that isn't newer, or a tag that already exists. `tag` refuses notes that still say `@NOTES@` and changes to anything but `VERSION` and the AppStream data, and makes nothing when it refuses. `tools.desktop-entry` fails whenever the newest AppStream release isn't `VERSION`'s or its notes are missing, so the two can't drift apart unnoticed.

The pushed tag starts the release workflow on GitHub ([`.github/workflows/release.yml`](../.github/workflows/release.yml)). It checks the tag with `tools/check-release-tag.sh`, which fails unless the tag is `v` + `VERSION` with that release the newest in the AppStream data and its notes written. Then it builds the source tarball, builds and tests Reverblabr from that tarball on Fedora, builds the Fedora packages from the same tarball and installs them in a clean container ([packaging/README.md](../packaging/README.md#fedora)), and creates a draft release on GitHub with the tarball, the RPMs, the source RPM and their checksums. The draft is published by hand after a last look. Publishing it sends the source RPM to COPR ([`.github/workflows/copr.yml`](../.github/workflows/copr.yml)), which builds and publishes the packages for each Fedora; until the repository has the `COPR_CONFIG` secret, that step says so and does nothing ([COPR](../packaging/README.md#copr)).

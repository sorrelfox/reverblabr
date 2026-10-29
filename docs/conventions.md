<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Coding conventions

These rules keep the code consistent across layers and contributors. Formatting is automated with [`.clang-format`](../.clang-format), so reviews can focus on everything else. For how the pieces fit together, see [architecture.md](architecture.md).

## Language and platform

- C++23, built with GCC 15+ or Clang 21+.
- Stay within the **Qt 6.10** and **KDE Frameworks 6.24** APIs, even when developing against newer versions. Check the "since" note in the documentation before using a recent function. `tools/container-check.sh ubuntu:26.04 test` proves a change still builds on the oldest supported platform.
- The layer rules in [architecture.md](architecture.md#layers) are enforced at configure time. Don't work around them. If a layer seems to need a forbidden dependency, the design needs a discussion.

## Files

- **File names** are lower case, with no separators: the header for the class `SessionModel` is `sessionmodel.h`, next to `sessionmodel.cpp`. Tests are `test_<name>.cpp` for GoogleTest and `tst_<name>.qml` for Qt Quick Test. QML components are `PascalCase.qml`.
- **Every file starts with an SPDX header.** C++ and QML files use the MPL-2.0 notice:

  ```cpp
  // SPDX-FileCopyrightText: 2026 Reverblabr contributors
  // SPDX-License-Identifier: MPL-2.0
  //
  // This Source Code Form is subject to the terms of the Mozilla Public
  // License, v. 2.0. If a copy of the MPL was not distributed with this
  // file, You can obtain one at https://mozilla.org/MPL/2.0/.
  ```

  Documentation uses CC-BY-SA-4.0 and configuration uses CC0-1.0; see [ADR-0005](adr/0005-licence-map.md).
- **Headers** use `#pragma once`.
- **Include paths** are written relative to `engine/`, for example `#include "dsp/decibel.h"`.
- **Include order,** in groups separated by blank lines; clang-format sorts within each group:
  1. the file's own header
  2. other project headers, in quotes
  3. third-party headers: Qt, KDE Frameworks, PipeWire, GoogleTest
  4. the C++ standard library

## Naming

| Kind | Style | Example |
|---|---|---|
| Namespaces | `rb::<layer>`, lower case | `rb::dsp`, `rb::core` |
| Types, enums, enumerators | `PascalCase` | `CompiledGraph`, `TopologyError::Cycle` |
| Functions, variables, parameters | `camelCase` | `topologicalOrder()`, `nodeCount` |
| Data members of classes | `m_` prefix | `m_sampleRate` |
| Static data members | `s_` prefix | `s_instanceCount` |
| Compile-time constants | `k` prefix | `kSilenceDb`, `kMaxFrames` |
| Macros (avoid them) | `RB_` prefix, upper case | `RB_NONBLOCKING` |
| Plain structs | public members without a prefix | `Edge::from` |

Name things for what they mean to a listener or a reader of the code, not for how they are implemented: `gainReductionDb`, not `grVal`. Units go in the name when they aren't obvious: `attackMs`, `levelDb`, `frames`.

## C++ rules

- **Ownership is explicit.** Use `std::unique_ptr` for ownership and references or raw pointers for non-owning access. No manual `new` or `delete` outside low-level containers. Qt objects may use Qt's parent ownership.
- **`[[nodiscard]]`** on functions whose result must be used, and always on functions that report errors.
- **`noexcept`** on everything that runs on the real-time thread, and on moves and destructors.
- **No `using namespace`** in headers. In a `.cpp` file it is fine for `Qt::StringLiterals` and for the file's own namespace in tests.
- **Types:** sample data is `float`; frame counts are `std::uint32_t` or `std::size_t`; sample rates are `double` or `std::uint32_t`, per API.
- **Prefer the standard library** in the engine layers: `std::span`, `std::expected`, `std::array`, `<algorithm>`. Qt types appear only from `engine/pw` upwards, where Qt is allowed.
- **A handle into a temporary is a use-after-free.** When a getter returns a `std::string_view`, a pointer or a reference into `*this`, and the type it belongs to is one that other code returns *by value*, then `thing().view()` leaves the handle pointing at an object that died at the semicolon. It reads exactly like the safe thing to write. It passes in debug, because the freed bytes still hold the old value; only the optimiser, or AddressSanitizer, ever says otherwise.

  This has been written three times here: `ParamValue::asText()` off `ParamStore::value()` (which sent the wrong device channel through every optimised build), and `Graph::findNode()` off `Graph::capture()` twice over. So:

  **Delete such a getter on an rvalue**, and let the compiler refuse the shape:

  ```cpp
  [[nodiscard]] std::optional<std::string_view> asText() const & noexcept;
  std::optional<std::string_view> asText() const && = delete;   // name the value first
  ```

  Offer an owning alternative where callers genuinely want the value of something they will not keep (`ParamValue::text()` returns a `std::string`), so that the safe spelling stays a one-liner.

### Errors

- **Errors are values.** A function that can fail returns `std::expected<T, E>` or an error code. `E` is an `enum class` inside the engine; at the facade it becomes an error code string.
- **Our code doesn't throw.** Exceptions stay enabled only because the standard library can throw, for example `std::bad_alloc` while preparing. Catch those at the layer boundary where they can occur, and turn them into error codes.
- **No exception may cross a layer boundary or reach the real-time thread.**
- **Error codes** are `<area>.<camelCaseName>`, for example `session.corruptFile`. Every code the engine can emit needs a translated message in the GUI's error table; a unit test checks this.

### Real-time code

Everything reachable from a process callback follows the [real-time rules](architecture.md#real-time-rules):

- no allocation or freeing
- no locks, system calls or logging
- no exceptions
- bounded work, and denormals flushed

Mark those functions `noexcept` and `RB_NONBLOCKING`. Test them with the `rtsan` preset, or with the allocation trap in the DSP test kit.

## Qt rules

- **String literals:** `u"text"_s` (with `using namespace Qt::StringLiterals`) or `QStringLiteral("text")`. Implicit conversions between `const char *` and `QString` are disabled on purpose.
- **Signals and slots:** use `Q_SIGNALS`, `Q_SLOTS` and `Q_EMIT` rather than the lower-case keywords (`QT_NO_KEYWORDS` is set). Connect with pointers to member functions, never with the string-based `SIGNAL()`/`SLOT()` macros.
- **Logging:** Qt logging categories named `reverblabr.<area>`; `qCDebug` for detail and `qCWarning` for problems. Log messages are English and are never shown in the interface.
- **Threads:** an object lives in one thread. Cross threads with queued connections or the engine's lock-free primitives, never by sharing mutable state.

## QML rules

- **Imports:**
  - `import org.kde.kirigami as Kirigami` (always namespaced)
  - `import org.kde.ki18n` for `KI18n.i18nc()`
  - `import io.github.reverblabr` for our own components
- **Every user-visible string** ([i18n.md](i18n.md)) goes through `KI18n.i18nc("<context marker>", "text")`, with a KDE context marker: `@title`, `@action:button`, `@label`, `@info`, `@info:tooltip`, and so on. Plurals use `KI18n.i18ncp()`. Never assemble sentences by concatenation; use placeholders (`%1`).
- **Spacing, sizes and colours** come from `Kirigami.Units` and `Kirigami.Theme`. There are no hard-coded pixel sizes or colours, except app-specific signal colours defined in one place.
- **Layout:** leave room for text about 40 % longer than English, and elide rather than clip.
- **Accessibility:** set `Accessible.name` on controls without visible text.
- **Keep logic in C++.** QML binds to properties and calls invokable methods; it holds no engine state of its own.
- **A control the session owns is written by a `SessionBound`, never by a binding.** A control assigns its own property the moment somebody works it (a `CheckBox` writes `checked`, a `ComboBox` `currentIndex`, a `Slider` `value`, a `TextField` `text`), and that assignment drops any binding that was drawing it from `SessionModel`. From then on it shows the last thing it was clicked to and no longer follows an undo, a second window, a session opened while the panel is showing, or the engine refusing the value. Nothing looks wrong until somebody undoes something, which is why three panels had it at once. So put the session's answer in a `SessionBound { control; property; value }` beside the control, give the control no binding for that property, and use `suspended` for the while somebody is dragging or typing. Putting the binding back by hand in every handler works and is the same care written once per handler instead of once per control; it was not kept because the cost of forgetting it is invisible.
- **Formatting:** `qmlformat` style, 4-space indentation. `qmllint -I build/<preset>/bin` must pass.

## Tests

- **Every change comes with tests,** and every bug fix with a test that failed before the fix.
- **Where tests live:**
  - GoogleTest cases in `tests/unit/<layer>/`, one executable per layer, registered with CTest as `<layer>.<Suite>.<Test>`
  - Qt Quick tests in `tests/qml/tst_<name>.qml`
  - integration tests against a private PipeWire instance in `tests/pw/`, using the fixture and helpers described in [tests/pw/README.md](../tests/pw/README.md)
- **Tests are deterministic.** Seed random generators with a fixed value, never depend on the wall clock, and don't sleep to wait for events: poll for the condition with a time limit.
- **Test the behaviour, not the implementation.** For DSP, prefer measured properties: levels, frequency response, maximum sample jump, latency.

### The DSP test kit

`tests/kit/` runs a block or a whole compiled graph offline, with no audio server anywhere near it. Link `reverblabr-dsp-testkit` and it gives you:

- **`signals.h`**: sine, sweep, white and pink noise, impulse, step, bursts, silence. Every generator is seeded, so a failure reproduces.
- **`analysis.h`**: peak, RMS, their decibel forms, a null test against a reference (the default tolerance is −90 dB), latency by correlating an impulse, the largest sample-to-sample step as a click detector, and a distortion estimate. `nullTestDb()` bottoms out at the −120 dB silence floor, so ask for exact equality when that is what you mean.
- **`offlinerunner.h`**: `BlockRunner` for one block, `GraphRunner` for a `CompiledGraph`, both able to run in fixed or randomly varying cycles.
- **`wavfile.h`**: WAV and FLAC through libsndfile, for a fixture or a golden file. libsndfile is a dependency of the engine since the Recorder, so these always run. **Fixtures are generated by the test or come from a CC0 source**; no recording of anyone else's is ever committed.
- **`allocationtrap.h`**: `NoAllocations`, which replaces the global `operator new` and `operator delete` and stops the test binary if a stretch of code takes or gives back memory. This is the real-time check for GCC, where `RB_NONBLOCKING` does nothing; under Address- and ThreadSanitizer it turns itself off, because the allocator is already theirs.

**Every DSP block has to pass `checkBlockSizeInvariance()`.** PipeWire hands a node whatever cycle length it likes and changes its mind while the session runs, so the same signal in different-sized pieces has to come out the same. A block that keeps anything per cycle rather than per sample fails it, which is what it is for.

## Commits

```
<area>: <what the commit does, imperative, at most 72 characters>

<Why the change is needed and what it does, wrapped at 72 columns.
Mention anything a reviewer might otherwise wonder about.>

Refs: #<issue>
Signed-off-by: Your Name <you@example.org>
```

- **`<area>`** is one of `build`, `tools`, `docs`, `dsp`, `graph`, `pw`, `engine`, `app`, `tests`, `ci`, `i18n`, `packaging`.
- **One logical change per commit.** Every commit builds and passes the tests, so `git bisect` works.
- **A commit may refer to a GitHub issue with `Refs: #N`**, and so may a pull request's description. Don't use `Fixes` or `Closes` in commit messages: `Fixes #N` in the pull request's description closes the issue when the pull request is merged.
- **Code, comments and documentation never cite issue numbers.** Say the reason instead, so they stay readable without the tracker.
- **`Signed-off-by` is required** (the Developer Certificate of Origin). `git commit -s` adds it. See [CONTRIBUTING.md](../CONTRIBUTING.md).
- **Write original wording.** Never copy text, names or assets from other products into code, comments, commit messages or documentation.

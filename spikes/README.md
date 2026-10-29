<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Spikes

Throwaway prototypes. A spike answers a design question and ends in a decision record; none of it is engine code, and none of it ships. Once the real code that replaces a prototype has landed, the prototype is deleted.

There are none here at the moment. A new one goes in a folder of its own, with `add_subdirectory(spikes)` in the top-level `CMakeLists.txt` and a `CMakeLists.txt` here to build it.

The earlier spikes are gone, as intended. What they found is kept in the decision records and in the notes in [`docs/dev/`](../docs/dev/), and the facts they established are checked again by tests against the real code:

- capture and linking: [ADR-0007](../docs/adr/0007-capture-and-linking.md), checked by `pw.capture-semantics`;
- the filter-node runtime: [ADR-0008](../docs/adr/0008-filter-runtime.md), checked by `pw.session-node`;
- canvas and meter rendering: [ADR-0009](../docs/adr/0009-canvas-rendering.md), measured by `tests/qml/tst_canvasframes.qml`;
- the hearing blocks' filterbank: [ADR-0019](../docs/adr/0019-hearing-filterbank.md), checked by the Hearing Compensation blocks' own tests.

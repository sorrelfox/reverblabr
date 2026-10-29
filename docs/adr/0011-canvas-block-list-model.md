<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0011: The canvas reads the session through a C++ list model

- **Status:** Accepted
- **Date:** 2026-09-21

## Context

`SessionModel` follows the engine's copy of the session: a snapshot when the session opens and a numbered patch for every edit after that ([ADR-0003](0003-in-process-engine-behind-facade.md)). What it tells the interface is one signal, `changed()`, which says that something did, not what.

The canvas draws one tile per block. A tile is not cheap: it carries a header, a face loaded from a component, its ports, and, for most blocks, a level meter that is a scene-graph item reading telemetry every frame ([ADR-0009](0009-canvas-rendering.md)). The budget is thirty such tiles at 60 fps, and the spike measured fifty at 1.3 ms per frame only because nothing in the scene was being rebuilt.

A `Repeater` over `SessionModel.blockIds` would rebuild all of them on every `changed()`. Every edit is a `changed()`, including a `moveBlock` that altered one tile's position by eight pixels, and including the `SetParam` a face sends while a slider on it is being dragged. Rebuilding takes the selection, the keyboard focus ring, an open rename and the drag that caused it with it, and it costs the frame budget exactly when a person is watching the canvas most closely.

## Decision

**The canvas reads a `QAbstractListModel` in C++, `rb::ui::BlockModel`.** It holds one row per block, with the roles `blockId`, `typeId`, `nickname`, `blockEnabled`, `position` and `unknownType`.

**It works out what changed rather than saying that something did.** On every `SessionModel::changed()` it reconciles its rows against the session: rows whose block has gone are removed, new ids are inserted at the index the session holds them at, a reordering is a row move, and a row whose data really differs gets a `dataChanged` for that row alone. Nothing resets the model, so a delegate that did not change is never rebuilt.

**It owns nothing.** The session model is still the one copy of what the session holds. `BlockModel` is a view of it and is told which session model to follow through a property, so a test can give it one of its own.

**Editing stays in `SessionModel`.** `addBlock`, `removeBlock`, `removeBlocks` and `moveBlock` join `renameBlock` and `setBlockEnabled` there: every one of them goes to the engine as an edit command and comes back as a patch. The list model is never written to.

**The role is `blockEnabled`, not `enabled`.** A delegate property called `enabled` shadows `QQuickItem::enabled`, and a tile whose bypass switched off its own mouse handling is a tile nobody can switch back on.

## Consequences

- The canvas's delegates survive every edit that does not concern them, which is what makes the 30-tile budget reachable at all.
- The order the session holds blocks in is the order the rows are in, which is the file's order and the canvas's Tab order. An undo that puts a removed block back in the middle puts its tile back in the middle.
- The reconcile is O(n²) in the worst case, on a list bounded by the session size (the canvas budget is about 150 blocks). It is run once per patch, not once per frame.
- Geometry the canvas has to compute over every block (fitting the view, deciding what a rubber band caught) reads `BlockModel::at()` rather than the delegates, so it does not depend on which tiles happen to have been built.
- Anything else that wants the session's blocks as a list (the wires, a future outline view) has one to use, rather than a second mirror of its own.

## Alternatives considered

- **A `Repeater` over `SessionModel.blockIds`.** Simplest, and wrong for the reason above: every edit rebuilds every tile.
- **A `QQmlListProperty` or a `QVariantList` property of block maps.** Still a whole-list assignment, so a view has no way to learn that one entry changed; it is the `Repeater` problem with more code.
- **Keeping the mirror in QML, in a `ListModel`, and applying the patches there.** That would put a second definition of what an edit does in JavaScript, beside the engine's `applyEdit` which `SessionModel` already uses. Two definitions of that is exactly what the patch protocol exists to avoid.
- **Making `SessionModel` itself the list model.** It is also the session's own properties (its name, its settings, its parameters), and a list model of blocks that is also the object holding `notes` is one class doing two jobs. Keeping them apart is also what lets a test drive the model with no session at all.

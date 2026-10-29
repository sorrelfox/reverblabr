<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0009: Canvas and meter rendering

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

The session canvas draws block tiles, the wires between them, a level meter in most tiles, a dot grid, and, in the Dynamics Processor's panel, a transfer curve with a marker that follows the audio. All of it has to stay at 60 frames a second while the meters move, on Plasma Wayland, at 100 % and 200 % scaling.

Spike S3 built a stand-in canvas and measured it. The prototype has since been deleted, as [the spikes' own rule](../../spikes/README.md) says it should be once what replaces it has landed; the real canvas is measured by `tests/qml/tst_canvasframes.qml`. The numbers are in [the spike notes](../dev/spike-s3-canvas.md), measured on Qt 6.11.2, Plasma 6.7.5 Wayland, Fedora 44, Radeon graphics, at the target scene: 50 tiles, 60 wires, 50 meters, the grid, and the curve with its marker, all of it on screen.

| What | Cost per frame on the GUI thread |
|---|---|
| The whole scene | **1.3 ms** (render thread 0.7 ms), at 100 % and at 200 % |
| The moving dash on 40 live wires | 0.7 ms of that |
| 60 wires themselves, without the dash | below 0.1 ms |
| 50 meters as scene-graph items | below 0.1 ms |
| The dot grid, as a shader or as a tiled image | below 0.1 ms either way |
| Rebuilding the transfer curve every frame | 0.15 ms |
| `CurveRenderer` instead of `GeometryRenderer` | 0.3 ms |
| Zooming the whole canvas continuously | below 0.1 ms, with either renderer |

No frame in any run was slower than 60 fps. Four times the scene (200 tiles, 240 wires, 200 meters) costs 5.0 ms on the GUI thread, and still holds the display's rate.

## Decision

**One ticker for the whole canvas.** A single `FrameAnimation` advances one property; every meter, dash and marker reads it. The canvas costs one timer, not one per block, and everything moves in step.

**Meters are scene-graph items.** Each meter is a `QQuickItem` that builds its bars in `updatePaintNode` (two bars and two peak marks, one geometry node, one flat-colour material). A new level costs a geometry update and nothing else: no new items, no bindings, no text layout. Peak hold and decay are computed in wall-clock time in the UI, from the telemetry snapshots.

**Wires are `Shape` items with `CurveRenderer`.** It costs 0.3 ms more per frame than the geometry renderer for 60 wires, and it keeps curves smooth at any zoom, because it shades them instead of flattening them into line segments. That is worth 0.3 ms out of a 16 ms budget.

**The moving dash is the one expensive effect, and it is budgeted.** It costs 0.7 ms per frame for 40 live wires, more than everything else together. It stays, because it is how a wire shows that audio flows, but:
- only wires that carry audio animate; idle wires are drawn solid
- the animation stops when the canvas is zoomed far out, when the window is not active, and when the session is stopped
- if a session ever has hundreds of live wires, the dash is dropped before the frame rate is

**The dot grid is a tiled image.** The shader and the tiled image cost the same, and the image needs no shader tooling, no `.qsb` files in the build, and renders identically at 200 % when the tile is drawn at the device pixel ratio. (`GridProvider` in the spike shows the shape of it.)

**The transfer curve is rebuilt only when its parameters change.** The marker is a separate item that moves on its own. Rebuilding the whole curve every frame costs 0.15 ms, which is affordable but pointless; the curve changes only when the user drags a handle.

**Budget.** The canvas keeps the GUI thread under 4 ms per frame for a session of up to about 150 blocks. Beyond that the measurements say the cost grows about linearly, so bigger sessions lose the dash first, then the meters' refresh rate (30 Hz instead of 60).

## Consequences

- The meter component is C++, not QML shapes or `Canvas`. Its interface is levels in and nothing else; the dB scale mapping stays in the UI layer around it.
- The canvas and wires can use `Shape` freely at this scene size; there is no need for a custom scene-graph node for wires.
- The telemetry sampler feeds one property per frame, and the QML reads it. There is no per-meter timer and no per-meter signal.
- Whoever adds an animated effect to the canvas measures it first: the dash shows that one such effect can cost more than the rest of the scene together.
- The spike's harness (`run-canvas-spike.sh`) stays as the canvas's performance test until the real canvas and the shared meter component land, and it skips where no compositor can be reached, so it is safe in CI.
- Qt 6.10, the minimum the project supports, was not measured: the development machine has 6.11.2. The techniques chosen (`Shape`, `FrameAnimation`, `updatePaintNode`, tiled images) all exist in 6.10, but the numbers should be confirmed when a 6.10 machine is available (the Kubuntu 26.04 baseline).

## Alternatives considered

- **Meters drawn in QML** (a `Rectangle` per bar, bound to a level): every level change touches the item tree and re-evaluates bindings. The scene-graph item keeps the per-frame work in one place and was already below 0.1 ms for 50 meters.
- **Meters on a `Canvas` item**: repaints through a raster paint engine, then uploads a texture; far more expensive than a geometry update.
- **A dot grid drawn with a shader**: the same cost as the tiled image, but it brings shader tooling into the build for no gain.
- **`GeometryRenderer` for wires**: 0.3 ms cheaper per frame, but curves are flattened at the zoom they were tessellated for, so they look coarse when the user zooms in.
- **A timer per meter**: 50 timers where one does, and the meters would not move in step.

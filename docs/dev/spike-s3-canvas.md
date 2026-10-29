<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Spike S3: canvas and meter rendering, the evidence

Measurements behind [ADR-0009](../adr/0009-canvas-rendering.md).

- **Machine:** Fedora 44, Plasma 6.7.5 on Wayland, Qt 6.11.2, AMD Ryzen 9 with Radeon graphics. Two screens; the window landed on the 75 Hz one, so a frame arrives every 13.3 ms rather than every 16.7 ms.
- **Scene:** 50 block tiles, 60 Bézier wires (40 of them "live", with a moving dash), 50 level meters, the dot grid, and a transfer curve with a marker. The canvas zooms out until the whole scene is on screen, so nothing is culled.
- **Prototype:** `spikes/s3-canvas/`, deleted once the canvas, wires and the meter component had landed. `Canvas.qml` was the scene, `MeterItem` a meter as a scene-graph item, `GridProvider` painted one dot-grid tile. Everything it measured is below; `tests/qml/tst_canvasframes.qml` asks the same questions of the real canvas.
- **What is timed:** every phase of a frame, from the window's own signals, with a nanosecond clock. The figure that matters is **CPU time per frame on each thread** (`CLOCK_THREAD_CPUTIME_ID`), because it counts work and not waiting. Qt's own timing messages are whole milliseconds, too coarse for a 16 ms budget.

To reproduce:

```sh
ctest --preset debug -R spike.s3-canvas --output-on-failure   # both scalings, with the thresholds
build/debug/bin/reverblabr-spike-canvas --help                # the variants below
```

The test skips (exit 77) when no compositor can be reached, so it is safe in CI and in a sandbox. It also skips in the `tsan` preset, where the instrumented binary can't start against the GPU driver; the measurement runs in `debug`, `release`, `asan` and `rtsan`.

## The scene, and what each part costs

Medians per frame, debug build, 4 seconds each (about 290 frames):

| Run | GUI thread | Render thread | Frames slower than 60 fps |
|---|---|---|---|
| Whole scene, 100 % | 1.32 ms | 0.66 ms | 0 of 289 |
| Whole scene, 200 % | 1.29 ms | 0.79 ms | 0 of 291 |
| `--dash static` | 0.64 ms | 0.68 ms | 0 of 291 |
| `--wires 0` | 0.69 ms | 0.49 ms | 0 of 291 |
| `--meters 0` | 1.24 ms | 0.64 ms | 0 of 291 |
| `--grid image` | 1.29 ms | 0.70 ms | 0 of 291 |
| `--grid none` | 1.34 ms | 0.62 ms | 0 of 291 |
| `--curve rebuilt` | 1.48 ms | 0.66 ms | 0 of 291 |
| `--renderer geometry` | 0.99 ms | 0.60 ms | 0 of 291 |

Reading the differences:

- **The moving dash costs 0.68 ms** (1.32 with it, 0.64 without). Animating `dashOffset` marks every live wire's path dirty, so all 40 are processed again each frame.
- **The wires themselves cost almost nothing.** Without the dash they are 0.64 ms, and removing all 60 wires saves only another 0.05 ms.
- **50 meters cost about 0.08 ms**, as scene-graph items whose `updatePaintNode` rewrites 24 vertices.
- **The dot grid costs about the same either way**, as a full-screen shader or as a tiled image: the difference between the three grid runs is inside the noise of these measurements (±0.05 ms).
- **Rebuilding the transfer curve every frame costs 0.15 ms** (121 points), so the difference between rebuilding and moving only the marker is real but small.
- **`CurveRenderer` costs 0.33 ms more than `GeometryRenderer`** for 60 wires plus the curve.

A release build measures the same, because nearly all the work is inside Qt's own libraries: 1.38 ms for the whole scene, 0.59 ms with a static dash, 4.96 ms at four times the scene.

## Zooming

Animating the canvas's zoom continuously, so every path is drawn at a new scale each frame:

| Run | GUI thread | Render thread |
|---|---|---|
| `--renderer curve --zoom static` | 1.24 ms | 0.69 ms |
| `--renderer curve --zoom animated` | 1.26 ms | 0.66 ms |
| `--renderer geometry --zoom static` | 0.99 ms | 0.74 ms |
| `--renderer geometry --zoom animated` | 1.06 ms | 0.68 ms |

Zooming costs almost nothing with either renderer: scaling an item is a transform on the scene-graph node, not a reason to rebuild paths. The renderers differ in how the result looks, not in what it costs here: `CurveRenderer` shades curves, so they stay smooth at any zoom, while `GeometryRenderer` flattens them into line segments at one scale. That is Qt's documented behaviour, not something these runs measured.

## How far it stretches

| Scene | GUI thread | Render thread | Frames slower than 60 fps |
|---|---|---|---|
| 50 tiles, 60 wires, 50 meters | 1.33 ms | 0.71 ms | 0 of 306 |
| 100 tiles, 120 wires, 100 meters | 2.51 ms | 1.52 ms | 0 of 290 |
| 200 tiles, 240 wires, 200 meters | 4.99 ms | 3.65 ms | 1 of 289 |
| 200 tiles, at 200 % | 5.04 ms | 3.68 ms | 1 of 289 |

The cost grows about linearly with the number of blocks, so the 4 ms budget for the GUI thread is reached at roughly 150 blocks. Even at four times the scene the display's rate still held, with one late frame in each run.

The worst case, with the zoom animating and the curve rebuilt every frame on top of the moving dash, stays at 1.33 ms: those costs do not add up the way they would if each one dirtied the whole scene.

## Notes for whoever builds the real canvas

- The measurement runs the scene zoomed to fit, so every tile is drawn. An earlier version left tiles outside the window, where Qt culls them, and reported costs that were too low.
- A stop that takes the samples' lock while the window is being stopped deadlocks: the render thread holds that lock in every frame, and stopping waits for the render thread. The prototype copies the samples first and prints afterwards.
- Qt 6.10, the project's minimum, was not measured; this machine has 6.11.2.

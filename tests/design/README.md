<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# The application against the design prototype

"Matches the design" as a number. The interactive prototype and the
application are drawn in the same named states, at the same size. Then they
are compared pixel by pixel.

```sh
ctest --preset design
# or, with its options:
tests/design/design_compare.py --build-dir build/debug [--appearance reverblabr-dark,reverblabr-light,system-dark,system-light]
```

The report is written to `build/debug/design/`:

- `report.html`: every state, with its image;
- `report.md`: the same tables in Markdown;
- `scores.tsv`: one line per state, for comparing two runs.

## What it needs

- **The prototype.** It is not in the repository.
  - The script needs its main HTML page. Without it the run is skipped (exit code 77).
  - `--prototype <page>` or `RB_DESIGN_PROTOTYPE=<page>` points the script at it.
- **Firefox.** It renders the prototype headless, through Firefox's own remote protocol, so neither geckodriver nor Selenium is needed.
- **The network**, the first time. The prototype loads React and its fonts from their CDNs.
- **The `reverblabr-design-grab` target.** CMake builds it with everything else.

If the prototype or Firefox is missing, or the page cannot be drawn, the run exits with code 77. ctest reports that as skipped, not failed.

**It is not part of the default run.** Every other test preset leaves out the `design` label, because the comparison needs things a normal build does not have and takes about half a minute.

## The states

The scene is one set of blocks at the same session coordinates on both sides:

| Row | Blocks |
|---|---|
| 1 | Application → Volume → Peak/RMS → Output Device |
| 2 | Input Device, on its own |
| 3 | System Audio, on its own |

Each appearance takes these screenshots:

| Screenshot | What it shows |
|---|---|
| `window-idle-<theme>` | The whole window, with the session stopped. |
| `window-live-<theme>` | The whole window, running. On the application's side every block is given a steady level of 0.5 through the mock engine. |
| `panel-<block>-<theme>` | That block's settings open beside it, with every folding section open. The prototype draws its panels open, so a folded panel would be scored on what it hides. |

The states are cut out of those screenshots:

- **`window-…`**: the whole screenshot.
- **`tile-<block>-<idle|live>-…`**: the tile.
- **`panel-<block>-…`**: the settings panel.

Each part is cut from both sides at **the prototype's size**, from the part's top-left corner. A tile or panel of another size shows up as a difference. It is not scaled to hide it.

**Masked**, on both sides: the session clock. It counts whatever time the run takes.

**The score** is the share of unmasked pixels that differ by more than `--tolerance` (24 of 255 by default) in any channel. 0 % is identical. Each image shows three panes:

1. the prototype;
2. the application;
3. the prototype dimmed, with the differing pixels in red and the masked pixels in blue.

## How each side is drawn

**The prototype**:

- served over HTTP from a local port;
- viewed in a 1440×900 window at one pixel per CSS pixel;
- driven into each state by handing its React component the state, rather than by clicking through;
- its clock and CSS animations stopped.

**The application**:

- `reverblabr-design-grab`, which is the real `Main` window against the mock engine;
- state kept in a temporary file;
- the window grabbed whole with `QQuickWindow::grabWindow()`.

It runs offscreen with an environment of its own, so nothing in your home directory is read or written:

- **The appearance.** `system-dark` and `system-light` are how the application follows the system today: Breeze with Plasma's Breeze Dark or Breeze Light colours, written to that environment's own `kdeglobals`.
- **The renderer.** When there is an X server, Qt Quick draws with OpenGL through it, as on the desktop. Nothing is shown on the screen. Qt's software renderer draws no custom scene-graph node, so without OpenGL the meters and tinted icons would be missing. The report says which renderer was used.

- **The application's own appearances**. `reverblabr-dark` and `reverblabr-light` are picked in `DesignGrab.qml`'s `applyAppearance()`, as the Settings menu picks them, and drawn with the Reverblabr control style, as the application starts in them. Each gets the Breeze scheme of its theme as well, so every run sits on a matching desktop and the scores stay comparable from one change to the next; since the style also draws the menus, tooltips and dialogs, nothing in the window takes the scheme's colours, which the QML suite's style run checks on a light desktop. All four appearances are compared by default.

## A state that gets further from the design fails the run

`thresholds.tsv` holds the worst each state may score, per appearance: its score when the file was last written, plus one percentage point. A run in which any state is worse fails, and says which. One point is room for a Firefox or Mesa update to move some antialiasing, and far less than any change of size or place.

A change that is meant to move a state rewrites the file:

```sh
tests/design/design_compare.py --build-dir build/debug --update-thresholds
```

Commit the file with the change, and say in the commit which states moved and why. A run that draws only some appearances keeps the others' lines as they were.

## The guide's screenshot

`docs/images/first-session.png`, in the README and the guide, is the application alone in a scene of its own, `docs/images/first-session.json`: the guide's first session, running, in the default look. It is drawn the way the comparison draws the application, so it needs no prototype, no sound and no desktop:

```sh
tests/design/design_compare.py --build-dir build/debug --grab-scene docs/images/first-session.json \
    --appearance reverblabr-dark --out build/debug/guide
cp build/debug/guide/first-session.png docs/images/
```

A scene may give a block its settings under `params`, as the first session names the application it plays.


A screenshot in such a scene may name a block type under `help`, such as `{ "name": "help-volume", "theme": "dark", "running": false, "panel": null, "help": "effect.volume" }`: the block's help window is then grabbed, 600 by 820 unless `width` and `height` say otherwise ([docs/dev/block-help.md](../../docs/dev/block-help.md)).

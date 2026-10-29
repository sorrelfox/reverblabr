#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""How close the application is to the design prototype, as a number.

One command renders the interactive prototype in a headless Firefox and the
application offscreen, in the same named states at the same size, and
compares them pixel by pixel:

    tests/design/design_compare.py --build-dir build/debug

For every state it writes the share of pixels that differ by more than a
small colour tolerance, and an image of where. **A state that is further from
the prototype than its line in `thresholds.tsv` fails the run**, so a
change that undoes the match cannot land unnoticed; see
tests/design/README.md.

**The prototype is not in the repository.** `--prototype` or
`RB_DESIGN_PROTOTYPE` names its main page. Without it, or without Firefox,
the run is skipped (exit code 77), which ctest reports as a skip rather than
a failure.
"""

import argparse
import configparser
import functools
import html
import http.server
import json
import os
import pathlib
import shutil
import subprocess
import sys
import threading
import urllib.parse

HERE = pathlib.Path(__file__).resolve().parent
SOURCE_DIR = HERE.parent.parent
sys.path.insert(0, str(HERE))
# The Firefox driver is imported from the source tree, which is no place for
# a compiled copy of it.
sys.dont_write_bytecode = True

SKIP = 77

# **What each state may score at worst**: the score it had when the
# file was last written, plus a margin. Written by `--update-thresholds`,
# read by every other run.
THRESHOLDS = HERE / "thresholds.tsv"
# How much worse than its recorded score a state may get, in percentage
# points, before the run fails. Enough for a Firefox or Mesa update to move
# some antialiasing; far less than any change of size or place does.
MARGIN_POINTS = 1.0

# **The one scene both sides draw.** The same blocks at the same places on
# the canvas, in session coordinates, so that a tile can be cut out of each
# side at the same size and laid over the other. Places are on the app's
# eight-pixel snap, so neither side moves a block to fit its own grid.
SCENE = {
    "window": {"width": 1440, "height": 900},
    "sessionName": "Design comparison",
    "grid": {"x": 40, "y": 88, "column": 160, "row": 120},
    "blocks": [
        {"name": "application", "prototype": "application", "type": "source.application", "column": 0, "row": 0},
        {"name": "volume", "prototype": "volume", "type": "effect.volume", "column": 1, "row": 0},
        {"name": "peakrms", "prototype": "peakrms", "type": "meter.peakrms", "column": 2, "row": 0},
        {"name": "output", "prototype": "outputdevice", "type": "output.device", "column": 3, "row": 0},
        {"name": "input", "prototype": "inputdevice", "type": "source.device", "column": 0, "row": 1},
        {"name": "system", "prototype": "systemaudio", "type": "source.system", "column": 0, "row": 2},
    ],
    "wires": [["application", "volume"], ["volume", "peakrms"], ["peakrms", "output"]],
    # What a live block is given on the application's side: a steady level,
    # so that its meters stand still while the screenshot is taken.
    "level": 0.5,
}

def screenshots():
    """Every screenshot a side takes: a theme, and what the window shows."""
    shots = []
    for theme in ("dark", "light"):
        for mode in ("idle", "live"):
            shots.append({"name": f"window-{mode}-{theme}", "theme": theme, "running": mode == "live", "panel": None})
        for block in SCENE["blocks"]:
            shots.append(
                {"name": f"panel-{block['name']}-{theme}", "theme": theme, "running": False, "panel": block["name"]}
            )
    return shots


def states():
    """Every state compared: which screenshot, and which part of it."""
    result = []
    for theme in ("dark", "light"):
        for mode in ("idle", "live"):
            result.append({"name": f"window-{mode}-{theme}", "shot": f"window-{mode}-{theme}", "rect": "window", "theme": theme})
        for mode in ("idle", "live"):
            for block in SCENE["blocks"]:
                result.append(
                    {
                        "name": f"tile-{block['name']}-{mode}-{theme}",
                        "shot": f"window-{mode}-{theme}",
                        "rect": f"tile:{block['name']}",
                        "theme": theme,
                    }
                )
        for block in SCENE["blocks"]:
            result.append(
                {"name": f"panel-{block['name']}-{theme}", "shot": f"panel-{block['name']}-{theme}", "rect": "panel", "theme": theme}
            )
    return result


# --------------------------------------------------------------------------
# The prototype


def find_prototype(explicit: str | None) -> pathlib.Path | None:
    """The prototype's main page, from --prototype or RB_DESIGN_PROTOTYPE,
    or None. The pages next to it are served with it."""
    given = explicit or os.environ.get("RB_DESIGN_PROTOTYPE")
    if not given:
        return None
    page = pathlib.Path(given).expanduser().resolve()
    return page if page.is_file() else None


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve(directory: pathlib.Path):
    """Serves \\a directory on a free local port. **Over HTTP, not from a
    file URL**: the prototype fetches its tile component as a second page,
    which a browser refuses to a page opened from disk."""
    handler = functools.partial(QuietHandler, directory=str(directory))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server


# Runs in the page. The prototype is a React class whose state is the whole
# interface, so a state is reached by handing it the state rather than by
# clicking through to it. **Its clock is stopped and its animations are
# switched off**, so two runs of one state are the same picture.
PAGE_DRIVER = r"""
const scene = arguments[0], shot = arguments[1], done = arguments[arguments.length - 1];
function logic() {
  const host = document.querySelector('#dc-root .sc-host');
  if (!host) return null;
  const key = Object.keys(host).find(k => k.startsWith('__reactFiber$'));
  for (let f = key && host[key]; f; f = f.return) {
    if (f.stateNode && f.stateNode.logic && f.stateNode.logic.renderVals) return f.stateNode.logic;
  }
  return null;
}
const L = logic();
if (!L || !L.state.sessions) { done({error: 'the prototype has not started'}); return; }
if (L.timer) { clearInterval(L.timer); L.timer = null; }
if (!document.getElementById('rb-still')) {
  const still = document.createElement('style');
  still.id = 'rb-still';
  still.textContent = '*,*::before,*::after{animation:none!important;transition:none!important;caret-color:transparent!important}';
  document.head.appendChild(still);
}
const g = scene.grid, ids = {};
let id = 0;
const blocks = scene.blocks.map(b => { ids[b.name] = ++id; return {id, key: b.prototype, x: g.x + b.column * g.column, y: g.y + b.row * g.row, on: true}; });
const wires = scene.wires.map(([f, t]) => [ids[f], ids[t]]);
L.nextId = id + 1;
const panel = shot.panel ? {id: ids[shot.panel], floating: false, x: 0, y: 0} : null;
L.setState({sessions: [{name: scene.sessionName, running: shot.running, startedAt: Date.now(), blocks, wires}],
            active: 0, theme: shot.theme, tick: 3, sel: null, panel, lib: 'blocks', query: '', zoom: 1,
            win: 'max', menu: null, drag: null, wireDrag: null, panelDrag: null});
const settle = n => n ? requestAnimationFrame(() => settle(n - 1)) : setTimeout(measure, 150);
document.fonts.ready.then(() => settle(6));
function box(r) { return [Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)]; }
function measure() {
  const rects = {window: [0, 0, window.innerWidth, window.innerHeight]};
  const cv = L.canvasRef.current.getBoundingClientRect();
  for (const b of scene.blocks) {
    const at = blocks[ids[b.name] - 1];
    rects['tile:' + b.name] = [Math.round(cv.left + at.x), Math.round(cv.top + at.y), 132, 96];
  }
  if (L.panelRef.current) rects.panel = box(L.panelRef.current.getBoundingClientRect());
  // The session clock counts whatever is done, so it is left out.
  const masks = [];
  for (const el of document.querySelectorAll('div,span')) {
    if (el.children.length === 0 && /^\d+:\d\d:\d\d$/.test(el.textContent.trim())) masks.push(box(el.getBoundingClientRect()));
  }
  done({rects, masks, fonts: [...document.fonts].filter(f => f.status === 'loaded').map(f => f.family + ' ' + f.weight)});
}
"""


def render_prototype(page: pathlib.Path, out: pathlib.Path, firefox: str) -> dict:
    from marionette import Firefox

    out.mkdir(parents=True, exist_ok=True)
    server = serve(page.parent)
    manifest = {}
    try:
        with Firefox(firefox) as browser:
            browser.set_viewport(SCENE["window"]["width"], SCENE["window"]["height"])
            url = f"http://127.0.0.1:{server.server_address[1]}/{urllib.parse.quote(page.name)}"
            browser.navigate(url)
            # React, its compiler and the fonts come from the network the
            # first time; the page is not drawn until they have.
            browser.async_script(
                """const done = arguments[arguments.length - 1];
                   const t0 = Date.now();
                   (function poll() {
                     const host = document.querySelector('#dc-root .sc-host');
                     if (host && host.querySelector('[data-screen-label]')) done(true);
                     else if (Date.now() - t0 > 50000) done(false);
                     else setTimeout(poll, 100);
                   })();""",
                timeout_ms=60000,
            )
            for shot in screenshots():
                result = browser.async_script(PAGE_DRIVER, SCENE, shot)
                if not result or "error" in result:
                    raise RuntimeError(f"prototype, {shot['name']}: {result and result.get('error')}")
                (out / f"{shot['name']}.png").write_bytes(browser.screenshot())
                manifest[shot["name"]] = {"file": f"{shot['name']}.png", "rects": result["rects"], "masks": result["masks"]}
                manifest.setdefault("_fonts", sorted(set(result["fonts"])))
    finally:
        server.shutdown()
    (out / "manifest.json").write_text(json.dumps(manifest, indent=1))
    return manifest


# --------------------------------------------------------------------------
# The application


def appearance_theme(appearance: str) -> str:
    return "light" if appearance.endswith("light") else "dark"


def app_environment(appearance: str, home: pathlib.Path) -> dict:
    """An environment of the grab's own: **nothing in the home directory of
    whoever runs it is read or written**, and the colour scheme is the one
    the appearance names rather than theirs."""
    env = dict(os.environ)
    for key in ("QT_QUICK_CONTROLS_STYLE", "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_FONT_DPI"):
        env.pop(key, None)
    env.update(
        {
            "QT_QPA_PLATFORM": "offscreen",
            # Qt writes to the journal when stderr is not a terminal; the log
            # belongs next to the screenshots.
            "QT_FORCE_STDERR_LOGGING": "1",
            "QT_ENABLE_HIGHDPI_SCALING": "0",
            "XDG_CONFIG_HOME": str(home / "config"),
            "XDG_DATA_HOME": str(home / "data"),
            "XDG_STATE_HOME": str(home / "state"),
            "XDG_CACHE_HOME": str(home / "cache"),
        }
    )
    # **Drawn by the GPU, as on the desktop.** Offscreen, Qt Quick falls back
    # to its software renderer, which does not draw a custom scene-graph
    # node at all: the meters vanish and the icons lose their tint. The
    # offscreen platform can reach OpenGL through the X server, when there
    # is one, and nothing is shown on it.
    if env.get("DISPLAY") and "QT_QUICK_BACKEND" not in os.environ:
        env["QT_QUICK_BACKEND"] = "rhi"
        env.setdefault("QSG_RHI_BACKEND", "opengl")
    for sub in ("config", "data", "state", "cache"):
        (home / sub).mkdir(parents=True, exist_ok=True)
    if appearance.startswith(("system-", "reverblabr-")):
        # **Follow system, as a Plasma desktop would have it**: Breeze, the
        # Breeze colours of that theme, its icons, and Plasma's default font.
        # The application's own appearances get the scheme of their theme
        # too, so each run sits on a matching desktop and the scores stay
        # comparable. The control style draws their menus, tooltips and
        # dialogs as well, so nothing in their window takes its colours.
        scheme = "BreezeDark" if appearance_theme(appearance) == "dark" else "BreezeLight"
        source = pathlib.Path("/usr/share/color-schemes") / f"{scheme}.colors"
        config = configparser.RawConfigParser(strict=False)
        config.optionxform = str
        if source.is_file():
            config.read(source, encoding="utf-8")
        config.setdefault("General", {})
        config["General"]["ColorScheme"] = scheme
        config.setdefault("Icons", {})
        config["Icons"]["Theme"] = "breeze-dark" if scheme == "BreezeDark" else "breeze"
        config.setdefault("KDE", {})
        config["KDE"]["widgetStyle"] = "Breeze"
        with open(home / "config" / "kdeglobals", "w", encoding="utf-8") as out:
            config.write(out, space_around_delimiters=False)
        env["QT_QPA_PLATFORMTHEME"] = "kde"
    return env


def grab_app(grabber: pathlib.Path, appearance: str, out: pathlib.Path) -> dict:
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    scene_file = out / "scene.json"
    scene_file.write_text(json.dumps({**SCENE, "screenshots": screenshots()}, indent=1))
    env = app_environment(appearance, out / "home")
    run = subprocess.run(
        [str(grabber), "--scene", str(scene_file), "--out", str(out), "--appearance", appearance],
        env=env, capture_output=True, text=True, timeout=300,
    )
    (out / "grab.log").write_text(run.stdout + run.stderr)
    if run.returncode != 0:
        raise RuntimeError(f"the application grab for {appearance} failed ({run.returncode}); see {out / 'grab.log'}")
    manifest = json.loads((out / "manifest.json").read_text())
    manifest["_renderer"] = (
        f"Qt Quick on {env['QSG_RHI_BACKEND']}, offscreen" if env.get("QT_QUICK_BACKEND") == "rhi"
        else "Qt Quick's software renderer, offscreen: **meters and tinted icons are missing**"
    )
    return manifest


# --------------------------------------------------------------------------
# The comparison


def crop(image, rect, size, fill):
    """\\a size pixels of \\a image from the top-left of \\a rect, padded with
    \\a fill where they run off the image."""
    import numpy

    width, height = size
    result = numpy.empty((height, width, 3), dtype=numpy.int16)
    result[:] = fill
    x, y = rect[0], rect[1]
    source = image[max(0, y):max(0, y + height), max(0, x):max(0, x + width)]
    result[max(0, -y):max(0, -y) + source.shape[0], max(0, -x):max(0, -x) + source.shape[1]] = source
    return result


def mask_for(masks, rect, size):
    import numpy

    width, height = size
    result = numpy.zeros((height, width), dtype=bool)
    for mx, my, mw, mh in masks:
        x0, y0 = mx - rect[0], my - rect[1]
        x1, y1 = max(0, x0), max(0, y0)
        x2, y2 = min(width, x0 + mw), min(height, y0 + mh)
        if x2 > x1 and y2 > y1:
            result[y1:y2, x1:x2] = True
    return result


def compare(prototype_dir, prototype, app_dir, app, appearance, out, tolerance):
    import numpy
    from PIL import Image

    out.mkdir(parents=True, exist_ok=True)
    theme = appearance_theme(appearance)
    rows = []
    for state in states():
        if state["theme"] != theme:
            continue
        ref_shot, app_shot = prototype.get(state["shot"]), app.get(state["shot"])
        ref_rect = ref_shot and ref_shot["rects"].get(state["rect"])
        app_rect = app_shot and app_shot["rects"].get(state["rect"])
        if not ref_rect or not app_rect:
            rows.append({"state": state["name"], "score": None, "note": "not reached on " + ("the prototype" if not ref_rect else "the application")})
            continue
        size = (ref_rect[2], ref_rect[3])
        ref_image = numpy.asarray(Image.open(prototype_dir / ref_shot["file"]).convert("RGB"), dtype=numpy.int16)
        app_image = numpy.asarray(Image.open(app_dir / app_shot["file"]).convert("RGB"), dtype=numpy.int16)
        a = crop(ref_image, ref_rect, size, (255, 0, 255))
        b = crop(app_image, app_rect, size, (255, 0, 255))
        masked = mask_for(ref_shot["masks"], ref_rect, size) | mask_for(app_shot["masks"], app_rect, size)
        differs = (numpy.abs(a - b).max(axis=2) > tolerance) & ~masked
        counted = int((~masked).sum())
        score = float(differs.sum()) / counted if counted else 0.0

        # Reference, application, and the reference dimmed with what
        # differs in red and what was not compared in blue.
        grey = (a.mean(axis=2, keepdims=True) * 0.35 + 40).repeat(3, axis=2)
        marked = grey.copy()
        marked[differs] = (230, 40, 40)
        marked[masked] = (40, 90, 230)
        gap = numpy.full((size[1], 8, 3), 128, dtype=numpy.int16)
        strip = numpy.concatenate([a, gap, b, gap, marked], axis=1).clip(0, 255).astype(numpy.uint8)
        Image.fromarray(strip).save(out / f"{state['name']}.png")
        rows.append({"state": state["name"], "score": score, "size": f"{size[0]}×{size[1]}"})
    return rows


def write_report(out, results, tolerance, fonts, renderer):
    lines = [
        "# Reverblabr against the design prototype",
        "",
        f"Share of pixels differing by more than {tolerance}/255 in any channel. **0 % is identical.**",
        "Each image is the prototype, the application, and where they differ (red) or were not compared (blue).",
        "",
    ]
    if fonts:
        lines += [f"Prototype: Firefox, headless, with {', '.join(fonts)} loaded.", ""]
    lines += [f"Application: {renderer}.", ""]
    tsv = ["appearance\tstate\tdiffering"]
    page = [
        "<!doctype html><meta charset=utf-8><title>Design comparison</title>",
        "<style>body{font:14px sans-serif;margin:24px;background:#fafafa}td,th{padding:4px 10px;text-align:left}"
        "img{max-width:100%;border:1px solid #ccc}section{margin:24px 0}</style>",
        "<h1>Reverblabr against the design prototype</h1>",
        f"<p>Share of pixels differing by more than {tolerance}/255. 0&nbsp;% is identical. "
        "Prototype · application · differences (red) and masked (blue).</p>",
    ]
    for appearance, rows in results.items():
        scored = [r for r in rows if r["score"] is not None]
        groups = {}
        for r in scored:
            groups.setdefault(r["state"].split("-")[0], []).append(r["score"])
        lines += [f"## {appearance}", "", "| Group | States | Mean differing |", "|---|---|---|"]
        for group, scores in groups.items():
            lines.append(f"| {group} | {len(scores)} | {100 * sum(scores) / len(scores):.1f} % |")
        lines += ["", "| State | Size | Differing |", "|---|---|---|"]
        page.append(f"<h2>{html.escape(appearance)}</h2><table><tr><th>State<th>Size<th>Differing</tr>")
        for r in rows:
            value = f"{100 * r['score']:.1f} %" if r["score"] is not None else r["note"]
            lines.append(f"| {r['state']} | {r.get('size', '')} | {value} |")
            tsv.append(f"{appearance}\t{r['state']}\t" + ("" if r["score"] is None else f"{r['score']:.4f}"))
            page.append(f"<tr><td>{r['state']}<td>{r.get('size', '')}<td>{html.escape(value)}</tr>")
        page.append("</table>")
        for r in scored:
            page.append(
                f"<section><h3>{r['state']} — {100 * r['score']:.1f} %</h3>"
                f"<img src='{html.escape(appearance)}/{r['state']}.png' loading=lazy></section>"
            )
        lines.append("")
    (out / "report.md").write_text("\n".join(lines))
    (out / "report.html").write_text("\n".join(page))
    (out / "scores.tsv").write_text("\n".join(tsv) + "\n")
    return "\n".join(lines)


def read_thresholds() -> dict:
    """The worst score each (appearance, state) may have, as a share."""
    limits = {}
    if not THRESHOLDS.is_file():
        return limits
    for line in THRESHOLDS.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        appearance, state, percent = line.split("\t")
        limits[(appearance, state)] = float(percent) / 100
    return limits


def write_thresholds(results: dict):
    """Records this run's scores, each plus the margin, as the new limits.
    **Rounded up to a tenth of a point**, so that a score sitting exactly on
    the line after rounding still passes. The appearances this run did not
    draw keep the limits they had."""
    import math

    limits = {key: round(100 * value, 1) for key, value in read_thresholds().items()}
    for appearance, rows in results.items():
        for row in rows:
            if row["score"] is not None:
                limits[(appearance, row["state"])] = math.ceil((100 * row["score"] + MARGIN_POINTS) * 10) / 10

    lines = [
        # Split, so that REUSE does not read these as this script's own tags.
        "# SPDX-" "FileCopyrightText: 2026 Reverblabr contributors",
        "# SPDX-" "License-Identifier: CC0-1.0",
        "#",
        "# The worst each state may score against the design prototype, in per cent of",
        "# its pixels: its score when this was written, plus "
        f"{MARGIN_POINTS:g} point. design_compare.py fails",
        "# a run in which any state is worse. After a change that is meant to move a",
        "# state, run `tests/design/design_compare.py --update-thresholds` and commit",
        "# this file with it, saying in the commit which states moved and why.",
        "#",
        "# appearance\tstate\tworst allowed (%)",
    ]
    for (appearance, state), limit in limits.items():
        lines.append(f"{appearance}\t{state}\t{limit:.1f}")
    THRESHOLDS.write_text("\n".join(lines) + "\n", encoding="utf-8")


def judge(results: dict, limits: dict) -> list:
    """The states that are worse than their limit, as lines to print."""
    worse = []
    for appearance, rows in results.items():
        for row in rows:
            limit = limits.get((appearance, row["state"]))
            if row["score"] is None or limit is None:
                continue
            if row["score"] > limit + 1e-9:
                worse.append(f"{appearance} {row['state']}: {100 * row['score']:.1f} %, "
                             f"allowed {100 * limit:.1f} %")
    return worse


def grab_scene(grabber: pathlib.Path, scene: pathlib.Path, appearance: str, out: pathlib.Path) -> int:
    """The application alone, in a scene of the caller's: the screenshots
    the scene names, drawn exactly as the comparison draws its own (GPU,
    the appearance's own colour scheme, a home of its own), but with no
    prototype to hold them against. The guide's screenshot is made this way
, from docs/images/first-session.json."""
    out.mkdir(parents=True, exist_ok=True)
    env = app_environment(appearance, out / "home")
    run = subprocess.run(
        [str(grabber), "--scene", str(scene), "--out", str(out), "--appearance", appearance],
        env=env, capture_output=True, text=True, timeout=300,
    )
    (out / "grab.log").write_text(run.stdout + run.stderr)
    if run.returncode != 0:
        print(f"FAIL: the grab failed ({run.returncode}); see {out / 'grab.log'}")
        return 1
    for shot in json.loads(scene.read_text())["screenshots"]:
        print(out / f"{shot['name']}.png")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--build-dir", default=str(SOURCE_DIR / "build" / "debug"),
                        help="the build tree holding reverblabr-design-grab")
    parser.add_argument("--grabber", help="the application grabber; found in --build-dir by default")
    parser.add_argument("--out", help="where screenshots and the report go; default <build-dir>/design")
    parser.add_argument("--prototype", help="the prototype's main page (default: $RB_DESIGN_PROTOTYPE)")
    parser.add_argument("--appearance", default="reverblabr-dark,reverblabr-light,system-dark,system-light",
                        help="the application's appearances to compare, comma-separated")
    parser.add_argument("--firefox", default=shutil.which("firefox") or "firefox")
    parser.add_argument("--tolerance", type=int, default=24,
                        help="how far apart two colours may be, out of 255 per channel, and still count as the same")
    parser.add_argument("--reuse-prototype", action="store_true",
                        help="compare against the prototype screenshots already in --out rather than rendering them again")
    parser.add_argument("--update-thresholds", action="store_true",
                        help="write this run's scores, plus the margin, to thresholds.tsv instead of judging them")
    parser.add_argument("--grab-scene", metavar="FILE",
                        help="only grab the application in this scene, in the first --appearance, into --out; "
                             "no prototype and no comparison (the guide's screenshot)")
    args = parser.parse_args()

    build = pathlib.Path(args.build_dir).resolve()
    out = pathlib.Path(args.out).resolve() if args.out else build / "design"
    grabber = pathlib.Path(args.grabber) if args.grabber else build / "bin" / "reverblabr-design-grab"

    if args.grab_scene:
        return grab_scene(grabber, pathlib.Path(args.grab_scene).resolve(), args.appearance.split(",")[0].strip(), out)
    prototype_out = out / "prototype"

    if args.reuse_prototype and (prototype_out / "manifest.json").is_file():
        prototype = json.loads((prototype_out / "manifest.json").read_text())
    else:
        source = find_prototype(args.prototype)
        if source is None:
            print("SKIP: no design prototype; name its main page with --prototype or RB_DESIGN_PROTOTYPE")
            return SKIP
        if not shutil.which(args.firefox):
            print("SKIP: no Firefox to render the prototype with")
            return SKIP
        try:
            prototype = render_prototype(source, prototype_out, args.firefox)
        except Exception as error:  # noqa: BLE001 — any failure to render is a reason to skip, said why
            print(f"SKIP: the prototype could not be rendered: {error}")
            return SKIP
    if not grabber.is_file():
        print(f"no application grabber at {grabber}; build the reverblabr-design-grab target", file=sys.stderr)
        return 1

    results = {}
    for appearance in [a.strip() for a in args.appearance.split(",") if a.strip()]:
        app_out = out / "app" / appearance
        app = grab_app(grabber, appearance, app_out)
        results[appearance] = compare(prototype_out, prototype, app_out, app, appearance, out / appearance, args.tolerance)
        renderer = app["_renderer"]
    print(write_report(out, results, args.tolerance, prototype.get("_fonts", []), renderer))
    print(f"report: {out / 'report.html'}")
    if args.update_thresholds:
        write_thresholds(results)
        print(f"thresholds written: {THRESHOLDS}")
        return 0
    worse = judge(results, read_thresholds())
    if worse:
        print("FAIL: further from the design than allowed (tests/design/thresholds.tsv):")
        print("\n".join("  " + line for line in worse))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

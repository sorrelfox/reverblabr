#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Holds a run's figures against a baseline and says what got worse.

A run is the JSON the benchmarks and the soak write (tests/perf/perfreport.h):
one file a suite, or a folder of them, or a file this script merged before.
Each figure says which way is better, and may say how far it may move the
wrong way (tolerance, a share of the baseline) and what change is too small
to count at all (noise, in its own unit). A figure that moved the wrong way
by more than both is a regression, and any regression makes the exit status 1.

Usage:
  tools/perf/report.py RUN [--baseline BASE] [--history OLDER ...]
                           [--threshold 0.10] [--merge OUT] [--markdown OUT]

  RUN, BASE, OLDER  a results folder (every *.json in it) or a merged file
  --threshold       the tolerance for figures that do not give their own
  --merge OUT       write RUN as one file, to keep as a later baseline
  --markdown OUT    write the comparison as Markdown ("-" for the output),
                    with a column for each --history run, oldest first

Without --baseline it only merges and lists. Runs from a different build
kind (release, debug) or another processor are compared all the same, with a
warning on top, and never fail: their differences are not the code's.
"""

import argparse
import json
import math
import pathlib
import sys

SCHEMA = 1


def load(path):
    """A run as {"suites": {suite: meta}, "metrics": {name: metric}}."""
    path = pathlib.Path(path)
    files = sorted(path.glob("*.json")) if path.is_dir() else [path]
    if not files:
        raise SystemExit(f"report: no results in {path}")
    run = {"schema": SCHEMA, "suites": {}, "metrics": {}}
    for file in files:
        data = json.loads(file.read_text(encoding="utf-8"))
        if data.get("schema") != SCHEMA:
            raise SystemExit(f"report: {file} is not a results file of schema {SCHEMA}")
        if "suites" in data:
            # Merged before.
            run["suites"].update(data["suites"])
            run["metrics"].update(data["metrics"])
            continue
        meta = {key: data.get(key) for key in ("build", "when", "host", "notes")}
        run["suites"][data["suite"]] = meta
        for name, metric in data.get("metrics", {}).items():
            run["metrics"][name] = dict(metric, suite=data["suite"])
    return run


def describe_host(meta):
    host = meta.get("host") or {}
    return f'{meta.get("build")} build, {host.get("cpu")}, {host.get("cores")} threads'


def mismatches(run, baseline):
    """What differs between the machines or builds the two runs came from."""
    found = []
    for suite, meta in run["suites"].items():
        base = baseline["suites"].get(suite)
        if base is None:
            continue
        if meta.get("build") != base.get("build"):
            found.append(f'{suite}: a {meta.get("build")} build against a {base.get("build")} one')
        if (meta.get("host") or {}).get("cpu") != (base.get("host") or {}).get("cpu"):
            found.append(f"{suite}: {describe_host(meta)} against {describe_host(base)}")
    return found


def judge(metric, base, threshold):
    """'regression', 'improvement', 'same', or None when it is not judged."""
    better = metric.get("better", "lower")
    value = metric.get("value")
    if better not in ("lower", "higher") or value is None or base is None:
        return None
    delta = value - base
    worse = delta > 0 if better == "lower" else delta < 0
    tolerance = metric.get("tolerance", threshold)
    noise = metric.get("noise", 0.0)
    if abs(delta) <= noise:
        return "same"
    relative = abs(delta) / abs(base) if base != 0 else math.inf
    if relative <= tolerance:
        return "same"
    return "regression" if worse else "improvement"


def change(value, base):
    if value is None or base is None:
        return ""
    if base == 0:
        return "new" if value != 0 else "0 %"
    return f"{(value - base) / abs(base) * 100:+.1f} %"


def number(value):
    if value is None:
        return "-"
    if value == 0 or abs(value) >= 100:
        return f"{value:.1f}"
    return f"{value:.3g}"


def compare(run, baseline, history, threshold):
    rows = []
    for name in sorted(run["metrics"]):
        metric = run["metrics"][name]
        base = baseline["metrics"].get(name, {}).get("value") if baseline else None
        older = [h["metrics"].get(name, {}).get("value") for h in history]
        rows.append(
            {
                "name": name,
                "unit": metric.get("unit", ""),
                "value": metric.get("value"),
                "base": base,
                "older": older,
                "verdict": judge(metric, base, threshold) if baseline else None,
            }
        )
    gone = sorted(set(baseline["metrics"]) - set(run["metrics"])) if baseline else []
    return rows, gone


def markdown(rows, gone, warnings, history_count, regressions):
    lines = ["# Performance report", ""]
    for warning in warnings:
        lines.append(f"> **Not comparable:** {warning}")
    if warnings:
        lines.append("")
    if regressions:
        lines.append(f"**{len(regressions)} regression(s):** " + ", ".join(f"`{r}`" for r in regressions))
    else:
        lines.append("No regressions.")
    lines.append("")
    older = "".join(f" | run -{history_count - index}" for index in range(history_count))
    lines.append(f"| Figure | Unit{older} | Baseline | This run | Change | |")
    lines.append("|---|---" + "|---:" * (history_count + 3) + "|---|")
    marks = {"regression": "worse", "improvement": "better", "same": "", None: ""}
    for row in rows:
        values = "".join(f" | {number(v)}" for v in row["older"])
        lines.append(
            f'| `{row["name"]}` | {row["unit"]}{values} | {number(row["base"])} | {number(row["value"])}'
            f' | {change(row["value"], row["base"])} | {marks[row["verdict"]]} |'
        )
    if gone:
        lines.append("")
        lines.append("In the baseline but not in this run: " + ", ".join(f"`{name}`" for name in gone))
    return "\n".join(lines) + "\n"


def main(argv):
    parser = argparse.ArgumentParser(description="Compare performance results with a baseline.")
    parser.add_argument("run")
    parser.add_argument("--baseline")
    parser.add_argument("--history", nargs="*", default=[])
    parser.add_argument("--threshold", type=float, default=0.10)
    parser.add_argument("--merge")
    parser.add_argument("--markdown")
    args = parser.parse_args(argv)

    run = load(args.run)
    if args.merge:
        pathlib.Path(args.merge).write_text(json.dumps(run, indent=1, sort_keys=True) + "\n", encoding="utf-8")

    baseline = load(args.baseline) if args.baseline else None
    history = [load(path) for path in args.history]
    warnings = mismatches(run, baseline) if baseline else []
    rows, gone = compare(run, baseline, history, args.threshold)
    regressions = [row["name"] for row in rows if row["verdict"] == "regression"]

    for warning in warnings:
        print(f"warning: not comparable: {warning}")
    for row in rows:
        verdict = f'  {row["verdict"].upper()}' if row["verdict"] in ("regression", "improvement") else ""
        base = f'  (baseline {number(row["base"])}, {change(row["value"], row["base"])})' if baseline else ""
        print(f'{row["name"]}: {number(row["value"])} {row["unit"]}{base}{verdict}')
    for name in gone:
        print(f"{name}: in the baseline, missing from this run")

    if args.markdown:
        text = markdown(rows, gone, warnings, len(history), regressions)
        if args.markdown == "-":
            sys.stdout.write(text)
        else:
            pathlib.Path(args.markdown).write_text(text, encoding="utf-8")

    if not baseline:
        print("no baseline: nothing compared")
        return 0
    if regressions and warnings:
        print(f"{len(regressions)} figure(s) worse, but the runs are not comparable: not failing")
        return 0
    if regressions:
        print(f"REGRESSION: {len(regressions)} figure(s) worse than the baseline allows: " + ", ".join(regressions))
        return 1
    print("no regressions")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

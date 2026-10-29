#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""tools/perf/report.py says a figure got worse when it did, and only then.

Made-up results first, one rule at a time. Then the whole path with the real
DSP benchmark: a block made slower on purpose (--inject-cost) has to fail the
comparison and be named, which is what the nightly run's alert rests on.

Usage: test_perf_report.py BENCH_DSP
"""

import json
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
REPORT = ROOT / "tools" / "perf" / "report.py"

HOST = {"cpu": "Test CPU", "cores": 8, "kernel": "1", "machine": "x86_64"}


def results(folder, suite, metrics, build="release", host=HOST):
    folder.mkdir(parents=True, exist_ok=True)
    data = {"schema": 1, "suite": suite, "build": build, "when": "now", "host": host, "notes": {}, "metrics": metrics}
    (folder / f"{suite}.json").write_text(json.dumps(data), encoding="utf-8")
    return folder


def report(*args):
    done = subprocess.run([sys.executable, str(REPORT), *map(str, args)], capture_output=True, text=True)
    return done.returncode, done.stdout + done.stderr


def figure(value, better="lower", **extra):
    return dict({"value": value, "unit": "% of a core", "better": better}, **extra)


failures = []


def expect(condition, message, output=""):
    if not condition:
        failures.append(message + ("\n" + output if output else ""))


with tempfile.TemporaryDirectory() as scratch:
    scratch = pathlib.Path(scratch)
    base = results(scratch / "base", "dsp", {"dsp.a": figure(1.0), "dsp.b": figure(2.0, noise=0.5)})

    # The same figures: nothing to say.
    code, out = report(base, "--baseline", base)
    expect(code == 0 and "no regressions" in out, "a run against itself failed", out)

    # A third slower, past the default tenth: a regression, by name.
    slower = results(scratch / "slower", "dsp", {"dsp.a": figure(1.3), "dsp.b": figure(2.0)})
    code, out = report(slower, "--baseline", base)
    expect(code == 1 and "dsp.a" in out and "REGRESSION" in out, "a third slower was not a regression", out)

    # Within the figure's own noise: not one, however large as a share.
    noisy = results(scratch / "noisy", "dsp", {"dsp.a": figure(1.0), "dsp.b": figure(2.4, noise=0.5)})
    code, out = report(noisy, "--baseline", base)
    expect(code == 0, "a change inside the noise failed", out)

    # A figure's own tolerance wins over the default.
    loose = results(scratch / "loose", "dsp", {"dsp.a": figure(1.3, tolerance=0.5), "dsp.b": figure(2.0)})
    code, out = report(loose, "--baseline", base)
    expect(code == 0, "a change inside the figure's own tolerance failed", out)

    # Faster is not a regression.
    faster = results(scratch / "faster", "dsp", {"dsp.a": figure(0.5), "dsp.b": figure(2.0)})
    code, out = report(faster, "--baseline", base)
    expect(code == 0 and "IMPROVEMENT" in out, "a faster run was not called better", out)

    # Higher is better: a drop is the regression.
    high = results(scratch / "high", "soak", {"soak.cycles-run": figure(100.0, better="higher", tolerance=0.01)})
    low = results(scratch / "low", "soak", {"soak.cycles-run": figure(90.0, better="higher", tolerance=0.01)})
    code, out = report(low, "--baseline", high)
    expect(code == 1, "fewer cycles run was not a regression", out)

    # From nothing to something, where nothing is the only right answer:
    # one descriptor more is a leak.
    none = results(scratch / "none", "soak", {"soak.fd-growth": figure(0, tolerance=0, noise=0)})
    one = results(scratch / "one", "soak", {"soak.fd-growth": figure(1, tolerance=0, noise=0)})
    code, out = report(one, "--baseline", none)
    expect(code == 1, "one more descriptor than none was not a regression", out)

    # Kept for the record only: never judged.
    short = results(scratch / "short", "soak", {"soak.seconds": figure(10, better="neither")})
    long = results(scratch / "long", "soak", {"soak.seconds": figure(1000, better="neither")})
    code, out = report(long, "--baseline", short)
    expect(code == 0, "a figure kept for the record was judged", out)

    # Another machine, or a debug build: compared, warned about, never failed.
    elsewhere = results(scratch / "elsewhere", "dsp", {"dsp.a": figure(3.0), "dsp.b": figure(2.0)},
                        host=dict(HOST, cpu="Other CPU"))
    code, out = report(elsewhere, "--baseline", base)
    expect(code == 0 and "not comparable" in out, "a run from another machine failed", out)
    debug = results(scratch / "debug", "dsp", {"dsp.a": figure(3.0), "dsp.b": figure(2.0)}, build="debug")
    code, out = report(debug, "--baseline", base)
    expect(code == 0 and "not comparable" in out, "a debug run against a release one failed", out)

    # Merged, it is a baseline as good as the folder, and the Markdown
    # names the regression and the history.
    merged = scratch / "merged.json"
    code, out = report(base, "--merge", merged)
    expect(code == 0 and merged.exists(), "the merge was not written", out)
    page = scratch / "report.md"
    code, out = report(slower, "--baseline", merged, "--history", base, base, "--markdown", page)
    text = page.read_text(encoding="utf-8") if page.exists() else ""
    expect(code == 1 and "1 regression" in text and "run -2" in text, "the Markdown report is wrong", text + out)

    # **The whole path**: the real benchmark, a block made slower on
    # purpose, and the report has to catch it. Reverb, because it costs
    # enough that doubling it is far outside any noise.
    if len(sys.argv) > 1:
        bench = sys.argv[1]
        before, after = scratch / "real-before", scratch / "real-after"
        for folder, extra in ((before, []), (after, ["--inject-cost", "100"])):
            folder.mkdir()
            done = subprocess.run(
                [bench, "--only", "effect.reverb", "--seconds", "2", "--repeats", "2", "--json",
                 str(folder / "dsp.json"), *extra],
                capture_output=True, text=True)
            expect(done.returncode == 0, "the benchmark failed", done.stdout + done.stderr)
        code, out = report(after, "--baseline", before)
        expect(code == 1 and "dsp.effect.reverb" in out, "a block made twice as slow was not caught", out)

if failures:
    for failure in failures:
        print("FAIL: " + failure)
    sys.exit(1)
print("PASS: perf report")

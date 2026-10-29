#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The key screens in the pseudo-locale.

Extracts the translation template from the sources as they are now, writes
the pseudo-translation of it (tools/pseudo-locale.py), compiles it, and runs
reverblabr-pseudo-check against it, which renders the screens offscreen and
reports untranslated or cut-off text. The template is made afresh on every
run, so a string added a minute ago is in it.

    pseudo_check.py --checker BIN --out DIR --appearance NAME [--inject FAULT --expect KIND]

With --inject, the run passes only if the checker catches the fault it was
given, and reports it as KIND: the check proving that it can fail.
"""

import argparse
import json
import pathlib
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--checker", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--appearance", required=True)
    parser.add_argument("--inject", default="")
    parser.add_argument("--expect", default="")
    options = parser.parse_args()

    out = pathlib.Path(options.out)
    shutil.rmtree(out, ignore_errors=True)
    catalog_dir = out / "locale" / "qps" / "LC_MESSAGES"
    catalog_dir.mkdir(parents=True)

    subprocess.run([str(REPO / "tools" / "extract-messages.sh"), str(out / "po")], check=True, stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, str(REPO / "tools" / "pseudo-locale.py"), str(out / "po" / "reverblabr.pot"),
                    str(out / "qps.po")], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(["msgfmt", "--check", "-o", str(catalog_dir / "reverblabr.mo"), str(out / "qps.po")], check=True)

    command = [options.checker, "--locale-dir", str(out / "locale"), "--out", str(out),
               "--appearance", options.appearance]
    if options.inject:
        command += ["--inject", options.inject]
    run = subprocess.run(command, timeout=600)
    report_file = out / "report.json"
    if run.returncode not in (0, 1) or not report_file.exists():
        print(f"FAIL: the checker stopped with {run.returncode} and no report")
        return 1
    report = json.loads(report_file.read_text(encoding="utf-8"))
    findings = report["findings"]

    if options.inject:
        caught = [f for f in findings if f["kind"] == options.expect and "injected" in f["path"]]
        if not caught:
            print(f"FAIL: the injected {options.inject} fault was not reported as {options.expect}")
            return 1
        print(f"PASS: pseudo-locale self-test ({options.inject} caught as {options.expect})")
        return 0

    if findings:
        print(f"FAIL: {len(findings)} findings on {report['screens']} screens; screenshots and report.json in {out}")
        return 1
    print(f"PASS: pseudo-locale, {report['screens']} screens, nothing untranslated or cut off")
    return 0


if __name__ == "__main__":
    sys.exit(main())

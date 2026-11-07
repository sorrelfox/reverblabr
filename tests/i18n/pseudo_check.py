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
    pseudo_check.py --checker BIN --out DIR --appearance NAME --catalog po/de/reverblabr.po

With --inject, the run passes only if the checker catches the fault it was
given, and reports it as KIND: the check proving that it can fail.

With --catalog, the screens are drawn in that real translation instead (its
Language header names it) and only text that is cut off is reported: whether
a language's words fit, the longest (German) and those in other scripts
(Japanese, Chinese) above all.
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
    parser.add_argument("--catalog", default="")
    options = parser.parse_args()

    out = pathlib.Path(options.out)
    shutil.rmtree(out, ignore_errors=True)
    if options.catalog:
        language = language_of(pathlib.Path(options.catalog))
        if not language:
            print(f"FAIL: {options.catalog} has no Language header")
            return 1
        source = pathlib.Path(options.catalog)
    else:
        language = "qps"
        subprocess.run([str(REPO / "tools" / "extract-messages.sh"), str(out / "po")], check=True,
                       stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable, str(REPO / "tools" / "pseudo-locale.py"), str(out / "po" / "reverblabr.pot"),
                        str(out / "qps.po")], check=True, stdout=subprocess.DEVNULL)
        source = out / "qps.po"
    catalog_dir = out / "locale" / language / "LC_MESSAGES"
    catalog_dir.mkdir(parents=True)
    subprocess.run(["msgfmt", "--check", "-o", str(catalog_dir / "reverblabr.mo"), str(source)], check=True)

    command = [options.checker, "--locale-dir", str(out / "locale"), "--language", language, "--out", str(out),
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
    if options.catalog:
        print(f"PASS: {language}, {report['screens']} screens, nothing cut off")
    else:
        print(f"PASS: pseudo-locale, {report['screens']} screens, nothing untranslated or cut off")
    return 0


def language_of(catalog: pathlib.Path) -> str:
    """The catalog's Language header: the folder its .mo goes in."""
    for line in catalog.read_text(encoding="utf-8").splitlines():
        if line.startswith('"Language:'):
            return line[len('"Language:'):].strip().rstrip('"').replace("\\n", "").strip()
        if line.startswith("#:") or (line.startswith("msgid") and line != 'msgid ""'):
            break
    return ""


if __name__ == "__main__":
    sys.exit(main())

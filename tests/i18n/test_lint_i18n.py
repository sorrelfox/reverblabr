#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Self-test for tools/lint-i18n.py.

Runs the lint on the fixtures in tests/i18n/lint/. In the bad fixtures, each
"expect-finding" marker on a line stands for one finding the lint must report
on that line; the good fixtures must produce no findings at all.
"""

import collections
import pathlib
import re
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
LINT = HERE.parent.parent / "tools" / "lint-i18n.py"
FINDING = re.compile(r"^(?P<path>.+?):(?P<line>\d+):\d+: ")


def run_lint(path):
    result = subprocess.run([sys.executable, str(LINT), str(path)], capture_output=True, text=True)
    counts = collections.Counter()
    for output_line in result.stdout.splitlines():
        match = FINDING.match(output_line)
        if not match:
            raise AssertionError(f"unexpected lint output: {output_line}")
        counts[int(match["line"])] += 1
    return result.returncode, counts, result.stdout


def expected_findings(path):
    counts = collections.Counter()
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        markers = line.count("expect-finding")
        if markers:
            counts[number] = markers
    return counts


def main():
    failures = []
    for fixture in sorted((HERE / "lint").iterdir()):
        status, found, output = run_lint(fixture)
        expected = expected_findings(fixture)
        if fixture.stem == "good":
            if status != 0 or found:
                failures.append(f"{fixture.name}: expected no findings, got:\n{output}")
        else:
            if status != 1:
                failures.append(f"{fixture.name}: expected exit status 1, got {status}")
            if found != expected:
                failures.append(f"{fixture.name}: findings per line {dict(sorted(found.items()))}, "
                                f"expected {dict(sorted(expected.items()))}\n{output}")
    for failure in failures:
        print("FAIL:", failure)
    if failures:
        return 1
    print("PASS: lint-i18n fixtures")
    return 0


if __name__ == "__main__":
    sys.exit(main())

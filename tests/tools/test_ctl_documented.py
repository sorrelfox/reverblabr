#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""reverblabr-ctl's promises to scripts, held to what it does.

A script relies on the exit codes and on the ctl.* error codes in the
--json output. Both are written down in docs/ctl.md, and the exit codes in
the man page too. This fails when the code has one the documents do not, or
the documents describe one with a different number or none in the code.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
FAILURE_H = REPO / "ctl" / "failure.h"
DOC = REPO / "docs" / "ctl.md"
MAN = REPO / "ctl" / "reverblabr-ctl.1"

failures = []

# enum class ExitCode : int { Ok = 0, Refused = 1, ... };
enum = re.search(r"enum class ExitCode : int \{(.*?)\};", FAILURE_H.read_text(), re.S)
exits = sorted(int(n) for n in re.findall(r"^\s*\w+ = (\d+),", enum[1], re.M)) if enum else []
if not exits:
    failures.append("no ExitCode values found in ctl/failure.h")

doc = DOC.read_text(encoding="utf-8")
exit_table = re.search(r"## Exit status\n(.*?)\n## ", doc, re.S)
documented = sorted(int(n) for n in re.findall(r"^\| (\d+) \|", exit_table[1], re.M)) if exit_table else []
if documented != exits:
    failures.append(f"docs/ctl.md lists exit codes {documented}, the tool has {exits}")

man = MAN.read_text(encoding="utf-8")
in_man = sorted(int(n) for n in re.findall(r"^\.B (\d+)$", man, re.M))
if in_man != exits:
    failures.append(f"the man page lists exit codes {in_man}, the tool has {exits}")

# Every ctl.* code the tool can send, and the documented ones with their exit.
used = set()
for source in (REPO / "ctl").glob("*.cpp"):
    used |= set(re.findall(r'u"(ctl\.[A-Za-z]+)"_s', source.read_text(encoding="utf-8")))
codes_table = dict(
    (code, int(exit_code))
    for code, exit_code in re.findall(r"^\| `(ctl\.[A-Za-z]+)` \|[^|]*\| (\d+) \|$", doc, re.M)
)
for code in sorted(used - codes_table.keys()):
    failures.append(f"{code} is sent by the tool and not in docs/ctl.md's table")
for code in sorted(codes_table.keys() - used):
    failures.append(f"{code} is in docs/ctl.md's table and the tool never sends it")
for code, exit_code in codes_table.items():
    if exit_code not in exits:
        failures.append(f"{code} is documented with exit {exit_code}, which the tool does not have")

if failures:
    print("\n".join(f"FAIL: {f}" for f in failures))
    sys.exit(1)
print("PASS: ctl documented")

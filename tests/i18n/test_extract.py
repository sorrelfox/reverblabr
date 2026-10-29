#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Checks that tools/extract-messages.sh finds the GUI's strings: C++ i18nc
calls, QML KI18n.i18nc calls and lazy kli18nc tables, each with its context,
and nothing from the engine, which must not produce user-visible text."""

import pathlib
import re
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
EXPECTED = [
    # (context, start of msgid, file the string comes from)
    ("@title:window", "Reverblabr", "app/qml/Main.qml"),
    ("@title", "Reverblabr", "app/main.cpp"),
    ("@info", "The connections in this session form a loop.", "app/errormessages.cpp"),
    ("@info KRunner", "Nothing is being recorded", "daemon/sessionrunner.cpp"),
]


def parse_pot(text):
    """Yields (references, msgctxt, msgid) for every entry of a .pot file."""
    for block in text.split("\n\n"):
        references, fields, current = [], {}, None
        for line in block.splitlines():
            if line.startswith("#:"):
                references += line[2:].split()
            elif match := re.match(r'^(msgctxt|msgid|msgid_plural|msgstr)\s+"(.*)"$', line):
                current = match[1]
                fields[current] = match[2]
            elif current and line.startswith('"'):
                fields[current] += line[1:-1]
        if fields.get("msgid"):
            yield references, fields.get("msgctxt"), fields["msgid"]


# reverblabr-ctl's own template: its gettext keywords, not KI18n's.
EXPECTED_CTL = [
    ("@info:usage", "Usage: reverblabr-ctl", "ctl/main.cpp"),
    ("@info session state", "running", "ctl/commands.cpp"),
]


def check(entries, expected, failures):
    for context, text, source in expected:
        if not any(ctx == context and msgid.startswith(text) and any(ref.startswith(source + ":") for ref in refs)
                   for refs, ctx, msgid in entries):
            failures.append(f'missing: msgctxt "{context}", msgid "{text}…" from {source}')
    for refs, ctx, msgid in entries:
        if ctx is None:
            failures.append(f'"{msgid}" has no context; use i18nc with a context marker')
        if any(ref.startswith("engine/") for ref in refs):
            failures.append(f'"{msgid}" comes from the engine, which must not produce user-visible text')


def main():
    with tempfile.TemporaryDirectory() as output:
        subprocess.run([str(REPO_ROOT / "tools" / "extract-messages.sh"), output], check=True, capture_output=True)
        entries = list(parse_pot((pathlib.Path(output) / "reverblabr.pot").read_text(encoding="utf-8")))
        ctl_entries = list(parse_pot((pathlib.Path(output) / "reverblabr-ctl.pot").read_text(encoding="utf-8")))

    failures = []
    check(entries, EXPECTED, failures)
    check(ctl_entries, EXPECTED_CTL, failures)
    if any(any(ref.startswith("ctl/") for ref in refs) for refs, _, _ in entries):
        failures.append("reverblabr-ctl's words are in the window's template; they belong in reverblabr-ctl.pot")

    for failure in failures:
        print("FAIL:", failure)
    if failures:
        return 1
    print(f"PASS: extraction ({len(entries)} messages, {len(ctl_entries)} of reverblabr-ctl's)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

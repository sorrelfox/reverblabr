#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The words the desktop shows before Reverblabr runs, translated.

The desktop entry's name, comment and actions, the notification events and
the AppStream data are extracted into reverblabr-data.pot, and each
po/<language>/reverblabr-data.po is merged back into the files the build
installs, by cmake/RbTranslateDataFile.cmake. Here: the template has them,
and a made-up German catalog, run through the very script the build runs,
comes out in all three files, which the distributions' own validators still
accept.

    test_data_translations.py CMAKE
"""

import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
DESKTOP = REPO_ROOT / "app" / "io.github.reverblabr.Reverblabr.desktop"
METAINFO = REPO_ROOT / "app" / "io.github.reverblabr.Reverblabr.metainfo.xml"
NOTIFYRC = REPO_ROOT / "notify" / "reverblabr.notifyrc"
RUNNER = REPO_ROOT / "daemon" / "io.github.reverblabr.Runner.desktop"
SCRIPT = REPO_ROOT / "cmake" / "RbTranslateDataFile.cmake"

# (start of msgid, file it comes from)
EXPECTED = [
    ("Capture, process and route audio on your desktop", "app/io.github.reverblabr.Reverblabr.desktop"),
    ("New Session", "app/io.github.reverblabr.Reverblabr.desktop"),
    ("All Sessions", "app/io.github.reverblabr.Reverblabr.desktop"),
    ("Recording finished", "notify/reverblabr.notifyrc"),
    ("A Recorder block has finished and saved its file", "notify/reverblabr.notifyrc"),
    ("Reverblabr Sessions", "daemon/io.github.reverblabr.Runner.desktop"),
    ("Run and stop Reverblabr sessions", "daemon/io.github.reverblabr.Runner.desktop"),
    ("Capture, process and route audio on your desktop", "app/io.github.reverblabr.Reverblabr.metainfo.xml"),
    ("A voice cleaned up, shaped and recorded", "app/io.github.reverblabr.Reverblabr.metainfo.xml"),
    ("The first version, not yet released.", "app/io.github.reverblabr.Reverblabr.metainfo.xml"),
]


def parse_pot(text):
    """Yields (references, msgid) for every entry of a .pot file but the header."""
    for block in text.split("\n\n"):
        references, msgid, current = [], None, None
        for line in block.splitlines():
            if line.startswith("#:"):
                references += line[2:].split()
            elif match := re.match(r'^(msgctxt|msgid|msgid_plural|msgstr)\s+"(.*)"$', line):
                current = match[1]
                if current == "msgid":
                    msgid = match[2]
            elif current == "msgid" and line.startswith('"'):
                msgid += line[1:-1]
        if msgid:
            yield references, msgid


def german(msgid):
    """A stand-in translation that can be told apart from the English."""
    return "«" + msgid + "»"


def write_catalog(entries, path):
    """A .po file translating every entry the template has."""
    lines = [
        'msgid ""',
        'msgstr ""',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Language: de\\n"',
        "",
    ]
    for _, msgid in entries:
        lines += [f'msgid "{msgid}"', f'msgstr "{german(msgid)}"', ""]
    path.parent.mkdir(parents=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def merge(cmake, kind, source, output, podir):
    run = subprocess.run(
        [cmake, f"-DKIND={kind}", f"-DSOURCE={source}", f"-DOUTPUT={output}", f"-DPODIR={podir}", "-P", str(SCRIPT)],
        capture_output=True, text=True, check=False,
    )
    return run.returncode, run.stdout + run.stderr


def main():
    cmake = sys.argv[1] if len(sys.argv) > 1 else "cmake"
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        subprocess.run([str(REPO_ROOT / "tools" / "extract-messages.sh"), str(scratch / "pot")],
                       check=True, capture_output=True)
        entries = list(parse_pot((scratch / "pot" / "reverblabr-data.pot").read_text(encoding="utf-8")))
        for text, source in EXPECTED:
            if not any(msgid.startswith(text) and any(ref.startswith(source + ":") for ref in refs)
                       for refs, msgid in entries):
                failures.append(f'missing from reverblabr-data.pot: "{text}…" from {source}')

        # **The build's own merge, with one language**, from KDE's layout.
        podir = scratch / "po"
        write_catalog(entries, podir / "de" / "reverblabr-data.po")
        out = scratch / "out"
        merged = {}
        for kind, source in (("DESKTOP", DESKTOP), ("DESKTOP", NOTIFYRC), ("DESKTOP", RUNNER), ("XML", METAINFO)):
            code, log = merge(cmake, kind, source, out / source.name, podir)
            if code != 0:
                failures.append(f"merging into {source.name} failed:\n{log}")
                continue
            merged[source.name] = (out / source.name).read_text(encoding="utf-8")

        desktop = merged.get(DESKTOP.name, "")
        for expected in (f"Comment[de]={german('Capture, process and route audio on your desktop')}",
                         f"Name[de]={german('New Session')}",
                         f"Name[de]={german('All Sessions')}"):
            if expected not in desktop:
                failures.append(f"the desktop entry has no {expected}")
        if f"Name[de]={german('Recording finished')}" not in merged.get(NOTIFYRC.name, ""):
            failures.append("the notification events have no German name")
        if f"Name[de]={german('Reverblabr Sessions')}" not in merged.get(RUNNER.name, ""):
            failures.append("the KRunner runner has no German name")
        metainfo = merged.get(METAINFO.name, "")
        if f'<summary xml:lang="de">{german("Capture, process and route audio on your desktop")}</summary>' \
                not in metainfo:
            failures.append("the AppStream data has no German summary")

        # **Still valid** once translated, as a distribution checks them.
        for tool, argv in (
            ("desktop-file-validate", ["desktop-file-validate", str(out / DESKTOP.name)]),
            ("appstreamcli", ["appstreamcli", "validate", "--no-net", str(out / METAINFO.name)]),
        ):
            if shutil.which(tool) is None:
                print(f"SKIP: {tool} is not installed")
                continue
            if not (out / argv[-1].split("/")[-1]).exists():
                continue
            run = subprocess.run(argv, capture_output=True, text=True, check=False)
            if run.returncode != 0:
                failures.append(f"{tool} rejects the translated file:\n{run.stdout}{run.stderr}")

        # **With no translations**, what is installed is the file as written.
        empty = scratch / "empty-po"
        empty.mkdir()
        code, log = merge(cmake, "DESKTOP", DESKTOP, scratch / "plain" / DESKTOP.name, empty)
        if code != 0 or (scratch / "plain" / DESKTOP.name).read_bytes() != DESKTOP.read_bytes():
            failures.append(f"with no translations the desktop entry changed:\n{log}")

    for failure in failures:
        print("FAIL:", failure)
    if failures:
        return 1
    print(f"PASS: data translations ({len(entries)} messages)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

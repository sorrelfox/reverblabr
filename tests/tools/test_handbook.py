#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The handbook, doc/index.docbook, against the program it describes.

    test_handbook.py blocks
        Every block the engine has and the library shows has a section of
        its own in the handbook, with the id "block-" and its type id
        (dashes for dots), titled with the name the library gives it; and
        no section describes a block that is not there any more. A block
        added, renamed or taken out without the handbook following fails.
        Plugin blocks share one section, which this leaves alone.

    test_handbook.py valid [CHECKER]
        The handbook against KDE's DocBook DTD, with checkXML6. Exits 77
        (skipped) without a CHECKER: KDocTools is not installed.
"""

import html
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
HANDBOOK = REPO / "doc" / "index.docbook"
PRESENTATION = REPO / "app" / "blockpresentation.cpp"
ENGINE_BLOCKS = REPO / "engine" / "blocks"


def library_blocks():
    """Type id → name, for the blocks the library describes."""
    # Entry{"type.id", "category", kli18nc("context", "Name"), ...}; the
    # context may run over two lines.
    entry = re.compile(r'Entry\{\s*"([a-z0-9.]+)",\s*"([a-z]+)",\s*kli18nc\(\s*"(?:[^"\\]|\\.)*",\s*"([^"]+)"\)')
    return {m[1]: (m[2], m[3]) for m in entry.finditer(PRESENTATION.read_text())}


def engine_blocks():
    """The type ids the engine's blocks declare."""
    declared = re.compile(r'k\w*TypeId\w*\s*=\s*"([^"]+)"')
    ids = set()
    for path in ENGINE_BLOCKS.glob("*.h"):
        ids.update(declared.findall(path.read_text()))
    return ids


def handbook_blocks():
    """Section id → title, for the block sections of the handbook."""
    section = re.compile(r'<sect2 id="block-([a-z0-9-]+)"[^>]*>\s*<title>(.*?)</title>', re.S)
    return {m[1]: html.unescape(m[2]) for m in section.finditer(HANDBOOK.read_text())}


def check_blocks():
    library = library_blocks()
    shown = {
        typeId: name
        for typeId, (category, name) in library.items()
        if typeId in engine_blocks() and category != "plugins"
    }
    if not shown:
        return [f"found no blocks in {PRESENTATION.relative_to(REPO)}: has its table changed shape?"]
    described = handbook_blocks()
    failures = []
    for typeId, name in sorted(shown.items()):
        key = typeId.replace(".", "-")
        if key not in described:
            failures.append(f'{name} ({typeId}) has no <sect2 id="block-{key}"> in the handbook')
        elif described[key] != name:
            failures.append(f'block-{key} is titled "{described[key]}", the library calls it "{name}"')
    known = {typeId.replace(".", "-") for typeId in shown}
    for key in sorted(set(described) - known):
        failures.append(f"block-{key} describes a block the library does not have")
    if failures:
        return failures
    print(f"{len(shown)} blocks, each with its section")
    return []


def check_valid(checker):
    if not checker:
        print("SKIP: checkXML6 was not found (KDocTools is not installed)")
        sys.exit(77)
    result = subprocess.run([checker, HANDBOOK.name], cwd=HANDBOOK.parent, capture_output=True, text=True)
    output = (result.stdout + result.stderr).strip()
    # checkXML6 says nothing about a valid document; anything it says is a
    # problem, even when it exits 0.
    if result.returncode != 0 or output:
        return [f"checkXML6 {HANDBOOK.relative_to(REPO)}:\n{output}"]
    return []


if len(sys.argv) == 2 and sys.argv[1] == "blocks":
    failures, passed = check_blocks(), "PASS: handbook blocks"
elif len(sys.argv) in (2, 3) and sys.argv[1] == "valid":
    failures, passed = check_valid(sys.argv[2] if len(sys.argv) == 3 else ""), "PASS: handbook valid"
else:
    print("usage: test_handbook.py blocks | valid [CHECKER]", file=sys.stderr)
    sys.exit(2)

if failures:
    print("\n".join(f"FAIL: {f}" for f in failures))
    sys.exit(1)
print(passed)

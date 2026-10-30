#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The blocks' help pages against the library, and against the format.

Every block the engine has and the library shows has a help page in
app/qml/BlockHelp.qml, or is listed in tests/tools/help-pending.txt as
still to write; no block is both, and neither names a block that is not
there. Plugin blocks are left alone, as the handbook's check leaves them.

Each page is filed under the block it says it describes, and **every
paragraph, point and name of it is one KI18n message on a line of its
own** (docs/adr/0029-block-help.md): a page that joined strings, or wrote
one without marking it, would reach translators in pieces or not at all.

That each page names every control of its panel is tst_blockhelp.qml's to
check, where the panel can be made.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
QML = REPO / "app" / "qml"
REGISTER = QML / "BlockHelp.qml"
PENDING = pathlib.Path(__file__).resolve().parent / "help-pending.txt"
PRESENTATION = REPO / "app" / "blockpresentation.cpp"
ENGINE_BLOCKS = REPO / "engine" / "blocks"

# The arrays a page's text is in, and one line of them: a KI18n call with a
# context and a text, nothing joined to it.
TEXT_LIST = re.compile(r"^\s*(paragraphs|points|closing|covers):\s*\[\s*$")
MESSAGE = re.compile(r'^\s*KI18n\.i18nc\("(?:[^"\\]|\\.)+",\s*"(?:[^"\\]|\\.)+"\),?\s*$')
ONE_LINE_LIST = re.compile(r"^\s*(paragraphs|points|closing|covers):\s*\[(.*)\]\s*$")


def library_blocks():
    """The type ids the library shows, as tools.handbook-blocks reads them."""
    entry = re.compile(r'Entry\{\s*"([a-z0-9.]+)",\s*"([a-z]+)",\s*kli18nc\(\s*"(?:[^"\\]|\\.)*",\s*"([^"]+)"\)')
    declared = re.compile(r'k\w*TypeId\w*\s*=\s*"([^"]+)"')
    engine = set()
    for path in ENGINE_BLOCKS.glob("*.h"):
        engine.update(declared.findall(path.read_text()))
    return {
        m[1] for m in entry.finditer(PRESENTATION.read_text()) if m[1] in engine and m[2] != "plugins"
    }


def registered_pages():
    """Type id → the page component's file, from the register."""
    text = REGISTER.read_text()
    components = dict(re.findall(r"readonly property Component (\w+): Component \{\s*(\w+) \{\}", text))
    pages = {}
    for typeId, name in re.findall(r'"([a-z0-9.]+)":\s*help\.(\w+)', text):
        pages[typeId] = QML / f"{components.get(name, name)}.qml"
    return pages


def pending_blocks():
    lines = PENDING.read_text().splitlines()
    return [line.strip() for line in lines if line.strip() and not line.startswith("#")]


def check_page(typeId, path):
    if not path.exists():
        return [f"{typeId}'s page {path.relative_to(REPO)} is not there"]
    failures = []
    text = path.read_text()
    filed = re.findall(r'^\s*typeId:\s*"([^"]+)"', text, re.M)
    if filed != [typeId]:
        failures.append(f"{path.name} says it describes {filed}, the register files it under {typeId}")
    inside = False
    for number, line in enumerate(text.splitlines(), start=1):
        where = f"{path.relative_to(REPO)}:{number}"
        if ONE_LINE_LIST.match(line) and not re.match(r"^\s*\w+:\s*\[\]\s*$", line):
            body = ONE_LINE_LIST.match(line)[2].strip()
            if not MESSAGE.match(body):
                failures.append(f"{where}: one message a line, marked with KI18n.i18nc: {line.strip()}")
            continue
        if TEXT_LIST.match(line):
            inside = True
            continue
        if inside:
            if re.match(r"^\s*\]", line):
                inside = False
            elif line.strip() and not line.strip().startswith("//") and not MESSAGE.match(line):
                failures.append(f"{where}: one message a line, marked with KI18n.i18nc: {line.strip()}")
        elif re.match(r'^\s*(title|summary):\s*"', line):
            failures.append(f"{where}: words not marked for translation: {line.strip()}")
    return failures


def main():
    library = library_blocks()
    if not library:
        print(f"FAIL: found no blocks in {PRESENTATION.relative_to(REPO)}: has its table changed shape?")
        return 1
    pages = registered_pages()
    if not pages:
        print(f"FAIL: found no pages in {REGISTER.relative_to(REPO)}: has the register changed shape?")
        return 1
    pending = pending_blocks()

    failures = []
    for typeId in sorted(set(pending)):
        if pending.count(typeId) > 1:
            failures.append(f"{typeId} is listed twice in {PENDING.name}")
    for typeId in sorted(library):
        if typeId in pages and typeId in pending:
            failures.append(f"{typeId} has a help page: take it out of {PENDING.name}")
        elif typeId not in pages and typeId not in pending:
            failures.append(f"{typeId} has no help page (app/qml/BlockHelp.qml), and is not in {PENDING.name}")
    for typeId in sorted(set(pages) - library):
        failures.append(f"BlockHelp.qml has a page for {typeId}, which the library does not have")
    for typeId in sorted(set(pending) - library):
        failures.append(f"{PENDING.name} lists {typeId}, which the library does not have")
    for typeId, path in sorted(pages.items()):
        failures.extend(check_page(typeId, path))

    if failures:
        print("\n".join(f"FAIL: {failure}" for failure in failures))
        return 1
    print(f"{len(library)} blocks: {len(pages)} with a help page, {len(set(pending))} still to write")
    print("PASS: help pages")
    return 0


sys.exit(main())

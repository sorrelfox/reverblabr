#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Prints a release's notes from the AppStream data, as Markdown.

The notes are written once, in the <release> entry tools/release.sh prepare
adds to the AppStream data, and a software centre shows them from there. A
GitHub Release takes the same text: each <p> becomes a paragraph and each
<li> a list item. Fails when there is no entry for the version or its notes
were never written.

Usage: tools/release-notes.py VERSION
"""

import pathlib
import sys
import xml.etree.ElementTree as ET

METAINFO = pathlib.Path(__file__).resolve().parent.parent / "app" / "io.github.reverblabr.Reverblabr.metainfo.xml"


def text(element):
    return " ".join("".join(element.itertext()).split())


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1])
    version = sys.argv[1]
    release = ET.parse(METAINFO).getroot().find(f"releases/release[@version='{version}']")
    if release is None:
        sys.exit(f"release-notes: no release {version} in {METAINFO.name}")
    description = release.find("description")
    blocks = []
    for child in [] if description is None else description:
        if child.tag == "p":
            blocks.append(text(child))
        elif child.tag in ("ul", "ol"):
            blocks.append("\n".join(f"- {text(item)}" for item in child.findall("li")))
    notes = "\n\n".join(block for block in blocks if block)
    if not notes or "@NOTES@" in notes:
        sys.exit(f"release-notes: the notes for {version} are not written")
    print(notes)


if __name__ == "__main__":
    main()

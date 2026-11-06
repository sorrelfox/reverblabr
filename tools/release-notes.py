#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Prints a release's text for GitHub, as Markdown.

First the release's notes from the AppStream data. They are written once,
in the <release> entry tools/release.sh prepare adds there, and a software
centre shows them from there: each <p> becomes a paragraph and each <li> a
list item. Then the downloads, from packaging/release-downloads.md: which
file is for which system, how to install and check it, and what each
package lacks. That file is written once for every release; its licence
header is left out here, and @VERSION@ becomes the version.

Fails when there is no entry for the version, its notes were never written,
or the downloads are missing or never name the version.

Usage: tools/release-notes.py VERSION
"""

import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
METAINFO = ROOT / "app" / "io.github.reverblabr.Reverblabr.metainfo.xml"
DOWNLOADS = ROOT / "packaging" / "release-downloads.md"

# The licence header every Markdown file starts with: an HTML comment with
# the SPDX tags in it.
LICENCE_HEADER = re.compile(r"\A\s*<!--(?:(?!-->).)*SPDX-(?:(?!-->).)*-->\s*", re.DOTALL)


def text(element):
    return " ".join("".join(element.itertext()).split())


def notes(version):
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
    written = "\n\n".join(block for block in blocks if block)
    if not written or "@NOTES@" in written:
        sys.exit(f"release-notes: the notes for {version} are not written")
    return written


def downloads(version):
    try:
        source = DOWNLOADS.read_text(encoding="utf-8")
    except OSError as error:
        sys.exit(f"release-notes: no downloads to add ({error.strerror}: {DOWNLOADS.relative_to(ROOT)})")
    if "@VERSION@" not in source:
        sys.exit(f"release-notes: {DOWNLOADS.relative_to(ROOT)} never names @VERSION@")
    return LICENCE_HEADER.sub("", source, count=1).replace("@VERSION@", version).strip()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1])
    version = sys.argv[1]
    print(notes(version) + "\n\n" + downloads(version))


if __name__ == "__main__":
    main()

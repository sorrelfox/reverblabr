#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The block folders hold to what ADR-0035 says of them.

- Every folder under blocks/ is added by blocks/CMakeLists.txt, every
  file in a block's folder is named in its CMakeLists.txt, and every
  entry (ui/<Name>Ui.qml) is listed in app/qml/BlockRegister.qml.
- **A block takes nothing from another block.** Its C++ includes its own
  folder's headers, blocks/shared's and the engine's, never another
  block's; its interface uses its own QML and C++ types and the
  interface's, never another block's. What two blocks share goes into
  blocks/shared, or into app/ for the interface. A block's tests may use
  other blocks, as a session does.
- blocks/shared takes nothing from any block.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
BLOCKS = REPO / "blocks"
INCLUDE = re.compile(r'^\s*#\s*include\s+"blocks/([^/"]+)/', re.M)
# A block's interface C++ is included by its name alone, as the QML type
# registration includes it.
BARE_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^/"]+\.h)"', re.M)
QML_ELEMENT = re.compile(r"class\s+(\w+)\b[^;{]*\{(?:(?!\nclass\s).)*?\bQML_ELEMENT\b", re.S)


def code_only(text):
    """The text without its comments and string literals, which may name anything."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'`":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(" ")
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main():
    failures = []
    folders = sorted(p for p in BLOCKS.iterdir() if p.is_dir())
    blocks = [p for p in folders if p.name != "shared"]
    top = (BLOCKS / "CMakeLists.txt").read_text(encoding="utf-8")
    added = set(re.findall(r"^\s*add_subdirectory\((\w+)\)", top, re.M))
    for folder in folders:
        if folder.name not in added:
            failures.append(f"blocks/{folder.name} is not added in blocks/CMakeLists.txt")
    for name in sorted(added - {p.name for p in folders}):
        failures.append(f"blocks/CMakeLists.txt adds blocks/{name}, which is not there")

    # Every file in a folder is named in its CMakeLists.txt.
    for folder in folders:
        cmake = folder / "CMakeLists.txt"
        if not cmake.exists():
            failures.append(f"blocks/{folder.name} has no CMakeLists.txt")
            continue
        named = set(re.findall(r"[\w./-]+\.\w+", cmake.read_text(encoding="utf-8")))
        for path in sorted(folder.rglob("*")):
            if path.is_file() and path != cmake:
                rel = path.relative_to(folder).as_posix()
                if rel not in named:
                    failures.append(f"blocks/{folder.name}/{rel} is not named in its CMakeLists.txt")

    # The types and the interface headers each block defines.
    owner = {}
    headers = {}
    for folder in blocks:
        for path in (folder / "ui").glob("*.h") if (folder / "ui").exists() else []:
            headers[path.name] = folder.name
        for path in (folder / "ui").glob("*.qml") if (folder / "ui").exists() else []:
            owner[path.stem] = folder.name
        for path in (folder / "ui").glob("*.h") if (folder / "ui").exists() else []:
            for match in QML_ELEMENT.finditer(path.read_text(encoding="utf-8")):
                owner[match[1]] = folder.name
    word = re.compile(r"\b(" + "|".join(sorted(owner, key=len, reverse=True)) + r")\b") if owner else None

    for folder in folders:
        for path in sorted(folder.rglob("*")):
            rel = path.relative_to(folder)
            if not path.is_file() or rel.parts[0] == "tests" or path.suffix not in (".h", ".cpp", ".qml"):
                continue
            text = path.read_text(encoding="utf-8")
            where = f"blocks/{folder.name}/{rel.as_posix()}"
            for other in INCLUDE.findall(text):
                if other != folder.name and not (other == "shared" and folder.name != "shared"):
                    failures.append(f"{where} includes from blocks/{other}")
            for header in BARE_INCLUDE.findall(text):
                if headers.get(header, folder.name) != folder.name:
                    failures.append(f"{where} includes {header}, which is blocks/{headers[header]}'s")
            # QML names types; C++ is held to its includes, as a C++ name such
            # as RNNoise's DenoiseState may look like a QML type of a block.
            if word and path.suffix == ".qml":
                for match in sorted(set(word.findall(code_only(text)))):
                    if owner[match] != folder.name:
                        failures.append(f"{where} uses {match}, which is blocks/{owner[match]}'s")

    listed = set(re.findall(r"^\s*(\w+Ui) \{\},?$", (REPO / "app" / "qml" / "BlockRegister.qml").read_text(encoding="utf-8"), re.M))
    for entry in sorted(BLOCKS.glob("*/ui/*Ui.qml")):
        if entry.stem not in listed:
            failures.append(f"{entry.relative_to(REPO)} is not listed in app/qml/BlockRegister.qml")

    for failure in failures:
        print("FAIL:", failure)
    print(f"{len(blocks)} block folders, {len(owner)} interface types: {len(failures)} problems")
    if not failures:
        print("PASS: block folders")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())

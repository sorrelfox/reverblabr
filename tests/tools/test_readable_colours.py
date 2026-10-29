#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Words are written in the colours made to be read.

The accent, the live orange and the ok, warning and danger colours are
fills: a button, a dot, a bar. Written as words on a light surface several of
them fall under WCAG AA's 4.5:1, so each has a colour for words made from it
(`AppColors.accentText`, `liveText`, `okText`, `warningText`, `dangerText`)
that clears it on every surface, and tst_contrast.qml measures. This check
is what keeps a label from being given the fill again: a text element's
`color` may not name one of the five.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
FOLDERS = [REPO / "app" / "qml", REPO / "app" / "style"]
TOKENS = {"AppColors.qml"}
TEXT = re.compile(r"^(Text|Label|QQC2\.Label|ElidedLabel|Kirigami\.Heading|Kirigami\.SelectableLabel|TextEdit|TextInput)\b")
FILLS = re.compile(r"AppColors\.(accent|live|ok|warning|danger)\b(?!\w)")

failures = []
checked = 0
for folder in FOLDERS:
    for path in sorted(folder.glob("*.qml")):
        if path.name in TOKENS:
            continue
        checked += 1
        lines = path.read_text(encoding="utf-8").split("\n")
        for index, line in enumerate(lines):
            code = line.split("//", 1)[0]
            match = re.match(r"^(\s*)color\s*:\s*(.*)", code)
            if not match or not FILLS.search(match.group(2)):
                continue
            indent = len(match.group(1))
            element = ""
            for back in range(index - 1, -1, -1):
                above = lines[back]
                depth = len(above) - len(above.lstrip())
                if depth < indent and re.match(r"^\s*[A-Z][\w.]*\s*\{", above):
                    element = above.strip().split()[0]
                    break
            if TEXT.match(element):
                failures.append(f"{path.relative_to(REPO)}:{index + 1}: words in a fill colour "
                                f"({element}): {line.strip()}")

if checked == 0:
    failures.append("no QML found")
if failures:
    print("\n".join(f"FAIL: {f}" for f in failures))
    sys.exit(1)
print(f"PASS: readable colours ({checked} files)")

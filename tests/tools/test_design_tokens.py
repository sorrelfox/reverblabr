#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Every colour, font and corner the interface draws with comes from a token.

The Reverblabr appearance (ADR-0012) switches the whole window between
the prototype's palettes and the desktop's by changing what `AppColors`,
`AppFonts` and `AppMetrics` say. A colour written straight into a component
would stay the same colour in all three, which is the one bug that switching
cannot show. So the QML outside those three files, the control style's
included, may name none: no hex
colour, no literal `Qt.rgba`, no font size, no scheme role or desktop font
read directly, and no corner radius as a number.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
QML = REPO / "app" / "qml"
# The control style draws with the same tokens, and is held to the
# same rule.
STYLE = REPO / "app" / "style"
TOKENS = {"AppColors.qml", "AppFonts.qml", "AppMetrics.qml"}

RULES = [
    (re.compile(r'"#[0-9A-Fa-f]{3,8}"'), "a colour written as hex"),
    (re.compile(r'Qt\.(rgba|hsla|hsva)\(\s*[0-9.]'), "a colour written as numbers"),
    (re.compile(r'color:\s*"(?!transparent")[a-z]+"'), "a colour written by name"),
    (re.compile(r'font\.(pixelSize|pointSize)\s*:'), "a font size"),
    (re.compile(r'Kirigami\.Theme\.(defaultFont|smallFont|fixedWidthFont)'), "the desktop's font, read directly"),
    # Read, not written: setting a scheme colour from a token
    # (`Kirigami.Theme.textColor: AppColors.danger`) is how a component
    # hands the desktop's style a colour of ours.
    (re.compile(r'Kirigami\.Theme\.\w+Color\b(?!\s*:)'), "a scheme colour, read directly"),
    (re.compile(r'\bradius:\s*[0-9]'), "a corner radius written as a number"),
    (re.compile(r'Kirigami\.Units\.cornerRadius'), "Kirigami's corner radius, read directly"),
]

failures = []
checked = 0
for path in sorted(QML.glob("*.qml")) + sorted(STYLE.glob("*.qml")):
    if path.name in TOKENS:
        continue
    checked += 1
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        code = line.split("//", 1)[0]
        for pattern, what in RULES:
            if pattern.search(code):
                failures.append(f"{path.relative_to(REPO)}:{number}: {what}: {line.strip()}")

if checked == 0:
    failures.append(f"no QML found under {QML.relative_to(REPO)}")

if failures:
    print("\n".join(failures))
    print(f"FAIL: {len(failures)} place(s) draw with something that is not a token")
    sys.exit(1)
print(f"PASS: design tokens ({checked} files)")

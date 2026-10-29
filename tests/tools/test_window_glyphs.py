#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The title bar's window buttons say what the desktop's say.

Beside every other window on Plasma, minimise is a chevron pointing down,
maximise a chevron pointing up, and a maximised window's restore button a
diamond. The design prototype has maximise and restore the other way round;
the title bar follows the desktop, which is what people read these by.
"""

import pathlib
import re
import sys

ICONS = pathlib.Path(__file__).resolve().parents[2] / "app" / "icons"

DOWN_CHEVRON = "M7 10l5 5 5-5"
UP_CHEVRON = "M7 14l5-5 5 5"
DIAMOND = "M4 8l8-4 8 4-8 4z"

EXPECTED = {
    "window-minimize": DOWN_CHEVRON,
    "window-maximize": UP_CHEVRON,
    "window-restore": DIAMOND,
}


def path_of(name: str) -> str:
    text = (ICONS / f"glyph.{name}.svg").read_text(encoding="utf-8")
    paths = re.findall(r'<path d="([^"]+)"', text)
    return " ".join(paths)


def main() -> int:
    failures = []
    for name, expected in EXPECTED.items():
        found = path_of(name)
        if found != expected:
            failures.append(f"glyph.{name}.svg draws '{found}', expected '{expected}'")
    for failure in failures:
        print("FAIL:", failure)
    if failures:
        return 1
    print("PASS: window glyphs")
    return 0


if __name__ == "__main__":
    sys.exit(main())

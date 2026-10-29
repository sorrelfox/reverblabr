#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Every package the project installs is explained to the person installing it.

tools/deps/fedora.txt and kubuntu.txt are what gets installed;
docs/dependencies.md is where a person learns what each package is for and
what goes missing without an optional one. A package added to a list and not
to the page is a dependency nobody was told about, so each one has to be
named on the page, in backticks, as the tables name them.
"""

import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
PAGE = REPO / "docs" / "dependencies.md"
LISTS = [REPO / "tools" / "deps" / "fedora.txt", REPO / "tools" / "deps" / "kubuntu.txt"]


def packages(path):
    """The package names in one list: each line up to its comment."""
    names = []
    for line in path.read_text().splitlines():
        name = line.split("#", 1)[0].strip()
        if name:
            names.append(name)
    return names


failures = []
if not PAGE.is_file():
    failures.append(f"{PAGE.relative_to(REPO)} is missing")
else:
    named = set(re.findall(r"`([^`\s]+)`", PAGE.read_text()))
    for path in LISTS:
        listed = packages(path)
        if not listed:
            failures.append(f"{path.relative_to(REPO)} lists no packages")
        for name in listed:
            if name not in named:
                failures.append(
                    f"{name} ({path.relative_to(REPO)}) is not in {PAGE.relative_to(REPO)}: "
                    "say what it is for, and whether it is optional"
                )

if failures:
    print("\n".join(f"FAIL: {f}" for f in failures))
    sys.exit(1)
print("PASS: dependencies documented")

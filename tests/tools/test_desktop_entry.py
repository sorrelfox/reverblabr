#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The application's identity, checked against itself.

Without a desktop entry the portal cannot find the application ("App info
not found"), the window has no icon and no task-manager entry, and on
Wayland the app id points at nothing. The entry only works if its file name,
its id inside the AppStream data and the name KAboutData carries are the
same string, so that is what this checks — along with running the two
validators when they are installed.
"""

import pathlib
import re
import shutil
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
APP_ID = "io.github.reverblabr.Reverblabr"
DESKTOP = REPO / "app" / f"{APP_ID}.desktop"
METAINFO = REPO / "app" / f"{APP_ID}.metainfo.xml"
MAIN = REPO / "app" / "main.cpp"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


check(DESKTOP.is_file(), f"{DESKTOP.relative_to(REPO)} is missing")
check(METAINFO.is_file(), f"{METAINFO.relative_to(REPO)} is missing")
if failures:
    print("\n".join(f"FAIL: {f}" for f in failures))
    sys.exit(1)

# The one string all three have to agree on.
named = re.search(r"setDesktopFileName\(u\"([^\"]+)\"_s\)", MAIN.read_text())
check(named is not None, "main.cpp does not call setDesktopFileName")
if named is not None:
    check(
        named.group(1) == APP_ID,
        f"main.cpp says the desktop file is {named.group(1)}, but it is {APP_ID}",
    )

metainfo = METAINFO.read_text()
check(f"<id>{APP_ID}</id>" in metainfo, f"the AppStream id is not {APP_ID}")
check(
    f"<launchable type=\"desktop-id\">{APP_ID}.desktop</launchable>" in metainfo,
    "the AppStream data does not launch the desktop entry",
)

# **What a software centre shows is what the tree has**: every
# screenshot the AppStream data points at is a file here, and the newest
# release is the version the build says it is.
RAW = "https://raw.githubusercontent.com/sorrelfox/reverblabr/main/"
for url in re.findall(r"<image[^>]*>([^<]+)</image>", metainfo):
    check(url.startswith(RAW), f"the screenshot {url} is not a file of this repository's main branch")
    if url.startswith(RAW):
        check((REPO / url[len(RAW):]).is_file(), f"the screenshot {url} is not in the tree")
# The version is VERSION's: its newest release has to be that one,
# with notes written for it.
releases = re.findall(r'<release version="([^"]+)"', metainfo)
version = (REPO / "VERSION").read_text().splitlines()[0].strip()
check(bool(releases), "the AppStream data has no release")
check(releases[:1] == [version],
      f"the newest release in the AppStream data is {releases[:1]}, VERSION says {version}: "
      "tools/release.sh prepare adds the entry")
newest = re.search(r"<release [^>]*>(.*?)</release>", metainfo, re.S)
check(newest is None or "@NOTES@" not in newest.group(1),
      "the newest release's notes were never written (it still says @NOTES@)")

desktop = DESKTOP.read_text()
check("Exec=reverblabr" in desktop, "the desktop entry does not run reverblabr")
check("Icon=" in desktop, "the desktop entry has no icon")

# The validators, when the machine has them. Not having them is not a
# failure: they are packaging tools, and this test is about the files
# agreeing with each other.
for tool, argv in (
    ("desktop-file-validate", ["desktop-file-validate", str(DESKTOP)]),
    ("appstreamcli", ["appstreamcli", "validate", "--no-net", str(METAINFO)]),
):
    if shutil.which(tool) is None:
        print(f"SKIP: {tool} is not installed")
        continue
    result = subprocess.run(argv, capture_output=True, text=True, check=False)
    check(result.returncode == 0, f"{tool} rejected the file:\n{result.stdout}{result.stderr}")

if failures:
    print("\n".join(f"FAIL: {f}" for f in failures))
    sys.exit(1)
print("PASS: desktop entry")

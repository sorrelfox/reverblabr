#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The panel widget's package is one Plasma can load.

Plasma finds a widget by its id, which has to be the name of the folder it
is installed in; it loads contents/ui/main.qml; and the widget explorer
shows the name, the description and the icon. The version is VERSION's,
as every program's is. Every file of the package in the source tree has to
be in the built one, since a file left out of plasmoid/CMakeLists.txt is
only noticed when the widget fails to load in somebody's panel.

Where kpackagetool6 is installed, it installs the package into a folder of
the test's own, which is what Plasma's own tooling checks a package with.

Usage: test_package.py PACKAGE SOURCE_PACKAGE ID VERSION_FILE [KPACKAGETOOL]
"""

import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

package = pathlib.Path(sys.argv[1])
source = pathlib.Path(sys.argv[2])
widget_id = sys.argv[3]
version = pathlib.Path(sys.argv[4]).read_text().splitlines()[0].strip()
kpackagetool = sys.argv[5] if len(sys.argv) > 5 else ""
failures = []

metadata = json.loads((package / "metadata.json").read_text())
plugin = metadata.get("KPlugin", {})
if metadata.get("KPackageStructure") != "Plasma/Applet":
    failures.append("metadata.json: KPackageStructure is not Plasma/Applet")
if plugin.get("Id") != widget_id:
    failures.append(f"metadata.json: Id is {plugin.get('Id')!r}, not {widget_id!r}")
if plugin.get("Version") != version:
    failures.append(f"metadata.json: Version is {plugin.get('Version')!r}, not VERSION's {version!r}")
for key in ("Name", "Description", "Icon", "Category", "License"):
    if not plugin.get(key):
        failures.append(f"metadata.json: {key} is missing")
if not metadata.get("X-Plasma-API-Minimum-Version", "").startswith("6."):
    failures.append("metadata.json: X-Plasma-API-Minimum-Version is not Plasma 6")

ui = package / "contents" / "ui"
if not (ui / "main.qml").is_file():
    failures.append("contents/ui/main.qml is missing")
for path in sorted((source / "contents").rglob("*")):
    if path.is_file() and not (package / path.relative_to(source)).is_file():
        failures.append(f"{path.relative_to(source)} is not in the built package")
for path in sorted(ui.glob("*.qml")):
    text = path.read_text()
    for script in re.findall(r'^import "([^"]+)"', text, re.MULTILINE):
        if not (ui / script).is_file():
            failures.append(f"{path.name} imports {script}, which the package does not have")
    if "KI18n." in text or "import org.kde.ki18n" in text:
        failures.append(f"{path.name}: a widget's words go through the global i18n calls, in its own domain")

if kpackagetool:
    with tempfile.TemporaryDirectory() as home:
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": home,
            "XDG_DATA_HOME": f"{home}/data",
            "XDG_CONFIG_HOME": f"{home}/config",
            "XDG_CACHE_HOME": f"{home}/cache",
            "QT_QPA_PLATFORM": "offscreen",
        }
        root = pathlib.Path(home) / "plasmoids"
        installed = subprocess.run(
            [kpackagetool, "--type", "Plasma/Applet", "--packageroot", str(root), "--install", str(package)],
            env=env, capture_output=True, text=True)
        if installed.returncode != 0:
            failures.append(f"kpackagetool6 refused the package: {installed.stdout}{installed.stderr}".strip())
        elif not (root / widget_id / "contents" / "ui" / "main.qml").is_file():
            failures.append(f"kpackagetool6 did not install the package as {widget_id}")
        described = subprocess.run([kpackagetool, "--appstream-metainfo", str(package)],
                                   env=env, capture_output=True, text=True)
        if described.returncode != 0 or f"<id>{widget_id}</id>" not in described.stdout:
            failures.append(f"kpackagetool6 could not describe the package: {described.stderr}".strip())
else:
    print("SKIP: kpackagetool6 is not installed; the metadata was checked without it")

if failures:
    print("\n".join(f"FAIL: {f}" for f in failures))
    sys.exit(1)
print(f"PASS: {widget_id} is a Plasma widget package")

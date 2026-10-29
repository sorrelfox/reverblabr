#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Every program says the version VERSION says.

Each is run with --version, and has to answer with VERSION's first line,
alone or with where the build is after a "+". The programs run offscreen
with a scratch home and a session bus that does not exist, so none of them
can reach the desktop, and --version is answered before any engine starts.
"""

import os
import pathlib
import re
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
version = (REPO / "VERSION").read_text().splitlines()[0].strip()
failures = []

with tempfile.TemporaryDirectory() as home:
    env = {
        "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
        "HOME": home,
        "XDG_CONFIG_HOME": f"{home}/config",
        "XDG_DATA_HOME": f"{home}/data",
        "XDG_STATE_HOME": f"{home}/state",
        "XDG_CACHE_HOME": f"{home}/cache",
        "XDG_RUNTIME_DIR": f"{home}/run",
        "DBUS_SESSION_BUS_ADDRESS": f"unix:path={home}/no-bus",
        "PIPEWIRE_REMOTE": "rb-test-no-such-socket",
        "QT_QPA_PLATFORM": "offscreen",
    }
    # **The sanitizers' own settings pass through**: under the asan preset
    # the leak suppressions are what keep Qt's known leaks from ending the
    # program before it has written its answer. They say nothing about
    # where the program may reach.
    for name in ("ASAN_OPTIONS", "LSAN_OPTIONS", "UBSAN_OPTIONS", "TSAN_OPTIONS", "RTSAN_OPTIONS", "PIPEWIRE_DLCLOSE"):
        if name in os.environ:
            env[name] = os.environ[name]
    for program in sys.argv[1:]:
        name = pathlib.Path(program).name
        try:
            result = subprocess.run([program, "--version"], env=env, capture_output=True, text=True, timeout=30)
        except subprocess.TimeoutExpired:
            failures.append(f"{name} --version did not answer within 30 s")
            continue
        answer = result.stdout.strip().splitlines()[:1]
        pattern = rf"^\S+ {re.escape(version)}(\+[0-9A-Za-z.]+)?$"
        if result.returncode != 0 or not answer or not re.match(pattern, answer[0]):
            failures.append(f"{name} --version answered {answer} (exit {result.returncode}); VERSION says {version}")
    written = [p for p in pathlib.Path(home).rglob("*") if p.is_file()]
    if written:
        failures.append(f"--version wrote files: {[str(p.relative_to(home)) for p in written]}")

if failures:
    print("\n".join(f"FAIL: {f}" for f in failures))
    sys.exit(1)
print(f"PASS: versions ({len(sys.argv) - 1} programs say {version})")

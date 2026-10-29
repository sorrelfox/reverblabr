#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The public D-Bus API, held to what has been released (ADR-0020).

io.github.reverblabr.Engine1 is only ever added to. The facade's own
conformance test keeps the C++ and the XML together, but says nothing when
both change together, which is exactly how a released signature would
break. So the released surface is written down in
dbus/io.github.reverblabr.Engine1.api, one line per method, signal, error
code and notice code, and this fails:

- when a pinned line is no longer carried exactly by the XML, by
  engine/core/errorcodes.h or by engine/core/noticecodes.h: a breaking
  change, which belongs in Engine2;
- when one of them carries something the pin does not: an addition,
  which is allowed, but made on purpose by adding its line;
- when docs/dbus-api.md does not name a pinned member or error code, or
  writes out a member's signature other than as released: nothing is
  public without being documented, and documented as it is.

    test_dbus_public_surface.py --write   adds the current additions to the
                                          pin (it refuses to drop a line)
"""

import pathlib
import re
import sys
import xml.etree.ElementTree as ElementTree

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
XML = REPO / "dbus" / "io.github.reverblabr.Engine1.xml"
ERRORS = REPO / "engine" / "core" / "errorcodes.h"
NOTICES = REPO / "engine" / "core" / "noticecodes.h"
PIN = REPO / "dbus" / "io.github.reverblabr.Engine1.api"
DOCS = REPO / "docs" / "dbus-api.md"
INTERFACE = "io.github.reverblabr.Engine1"
# **Codes the window reports about itself, which no engine ever sends**, so
# not the API's. A new code is pinned or listed here, on purpose: either way
# somebody decided.
WINDOW_ONLY = {
    # The engine in reverblabrd could not be reached: by definition,
    # not something reverblabrd says.
    "engine.daemonUnreachable",
}

PIN_HEADER = """\
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MIT
#
# io.github.reverblabr.Engine1 as released (ADR-0020): every method and
# signal with its arguments, and every error code the Error signal and
# notice code the Notice signal carries, with the names in its argsJson. Lines are only ever added. A line that no
# longer matches is a breaking change, and belongs in Engine2.
# dbus.public-surface (tests/tools/test_dbus_public_surface.py) holds the
# XML, engine/core/errorcodes.h and docs/dbus-api.md to this file.
"""


def surface():
    """The current surface: {line: name}, in the XML's and the header's order."""
    lines = {}
    root = ElementTree.parse(XML).getroot()
    interfaces = [node for node in root.iter("interface") if node.get("name") == INTERFACE]
    if len(interfaces) != 1:
        raise SystemExit(f"FAIL: {XML.relative_to(REPO)} has no interface {INTERFACE}")
    for member in interfaces[0]:
        if member.tag not in ("method", "signal"):
            continue
        arguments = []
        for argument in member.iter("arg"):
            text = f"{argument.get('type')} {argument.get('name')}"
            if member.tag == "method":
                text = f"{argument.get('direction', 'in')} {text}"
            arguments.append(text)
        lines[f"{member.tag} {member.get('name')}({', '.join(arguments)})"] = member.get("name")
    pattern = re.compile(r'ErrorCodeInfo\{ErrorCode::\w+, "([^"]+)"(?:, \{([^}]*)\})?\}')
    for code, arguments in pattern.findall(ERRORS.read_text()):
        if code in WINDOW_ONLY:
            continue
        names = [name.strip().strip('"') for name in arguments.split(",") if name.strip()]
        lines[f"error {code}({', '.join(names)})"] = code
    # **What the Notice signal carries** is as public as what Error
    # carries.
    pattern = re.compile(r'NoticeCodeInfo\{NoticeCode::\w+, "([^"]+)"(?:, \{([^}]*)\})?\}')
    for code, arguments in pattern.findall(NOTICES.read_text()):
        names = [name.strip().strip('"') for name in arguments.split(",") if name.strip()]
        lines[f"notice {code}({', '.join(names)})"] = code
    return lines


def pinned():
    if not PIN.is_file():
        return []
    return [line.strip() for line in PIN.read_text().splitlines() if line.strip() and not line.startswith("#")]


def main():
    current = surface()
    released = pinned()

    if "--write" in sys.argv:
        missing = [line for line in released if line not in current]
        if missing:
            print("\n".join(f"FAIL: breaking change, not written: {line}" for line in missing))
            return 1
        added = [line for line in current if line not in released]
        PIN.write_text(PIN_HEADER + "\n" + "\n".join(released + added) + "\n")
        print(f"wrote {len(added)} new line(s) to {PIN.relative_to(REPO)}")
        return 0

    failures = []
    if not released:
        failures.append(f"{PIN.relative_to(REPO)} is missing or empty")
    for line in released:
        if line not in current:
            failures.append(
                f"breaking change: '{line}' is released in {INTERFACE} and is no longer there as it was. "
                "Engine1 is only ever added to (ADR-0020); a change that is not an addition belongs in Engine2"
            )
    for line in current:
        if line not in released:
            failures.append(
                f"not pinned: '{line}'. An addition is allowed: add the line to {PIN.relative_to(REPO)} "
                "(or run this script with --write) and document it in docs/dbus-api.md"
            )
    docs = DOCS.read_text() if DOCS.is_file() else ""
    documented = set(re.findall(r"`([^`\s(]+)", docs))
    for line in released:
        name = line.split(" ", 1)[1].split("(", 1)[0]
        if name not in documented:
            failures.append(f"not documented: {name} is public and docs/dbus-api.md never names it (`{name}`)")
    # **A signature the docs write out is the released one**: `Name(s a, t b)`
    # with a type and a name for every argument, as the tables give them.
    signatures = {}
    for line in released:
        kind, rest = line.split(" ", 1)
        if kind in ("error", "notice"):
            continue
        name, arguments = rest[:-1].split("(", 1)
        signatures[name] = [" ".join(argument.split()[-2:]) for argument in arguments.split(", ") if argument]
    for name, arguments in re.findall(r"`([A-Z]\w*)\(([^)`]*)\)`", docs):
        written = [argument.strip() for argument in arguments.split(",") if argument.strip()]
        if name in signatures and all(len(argument.split()) == 2 for argument in written):
            if written != signatures[name]:
                failures.append(
                    f"documented wrongly: docs/dbus-api.md writes {name}({', '.join(written)}), "
                    f"released as {name}({', '.join(signatures[name])})"
                )

    if failures:
        print("\n".join(f"FAIL: {failure}" for failure in failures))
        return 1
    print(f"PASS: public D-Bus surface ({len(released)} lines)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

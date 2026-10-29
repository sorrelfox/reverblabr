#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Writes a pseudo-translation of a gettext template.

Every message comes out wrapped in ⟦ ⟧, with its letters accented and about
40 % longer, so that a screen rendered in it shows at a glance which words
never went through the catalog (no brackets) and which ones no longer fit
(the longer text is cut off). It is for the tests only, never
installed.

What a translation must keep is kept as it is: KI18n placeholders (%1, %2),
the file-name tokens of the Recorder (%year, %session), markup tags and
entities, and the keyboard accelerator's ampersand.

Usage: tools/pseudo-locale.py TEMPLATE.pot OUTPUT.po
"""

import re
import sys

OPEN, CLOSE = "⟦", "⟧"
GROWTH = 0.4

ACCENTS = str.maketrans(
    "aeiouyAEIOUYcnszCNSZ",
    "áéíóúýÁÉÍÓÚÝçñšžÇÑŠŽ",
)
PADDING = "ẋǿẋǿ"

# Kept verbatim: placeholders and tokens, tags, entities, escapes.
KEEP = re.compile(r"%\w+|<[^>]*>|&[a-zA-Z]+;|&#\d+;|\\.")


def pseudo(text: str) -> str:
    """The pseudo-translation of one message's text (still escaped as in the .po)."""
    if not text:
        return text
    parts = []
    visible = 0
    position = 0
    for match in KEEP.finditer(text):
        plain = text[position : match.start()]
        parts.append(plain.translate(ACCENTS))
        visible += len(plain)
        parts.append(match.group(0))
        position = match.end()
    tail = text[position:]
    parts.append(tail.translate(ACCENTS))
    visible += len(tail)
    grow = max(1, round(visible * GROWTH)) if visible else 0
    padding = (PADDING * (grow // len(PADDING) + 1))[:grow]
    return OPEN + "".join(parts) + padding + CLOSE


def unquote(line: str) -> str:
    """The text of a "…" line of a .po file, still escaped."""
    line = line.strip()
    return line[1:-1]


def read_entries(path: str):
    """Each entry as a dict of its keywords (msgctxt, msgid, msgid_plural) to texts."""
    entry, key = {}, None
    with open(path, encoding="utf-8") as handle:
        for raw in handle:
            line = raw.rstrip("\n")
            if not line.strip():
                if entry:
                    yield entry
                entry, key = {}, None
                continue
            if line.startswith("#"):
                continue
            if line.startswith('"'):
                entry[key] += unquote(line)
                continue
            keyword, _, rest = line.partition(" ")
            key = keyword  # msgstr and msgstr[n] are collected too, and ignored
            entry[key] = unquote(rest)
    if entry:
        yield entry


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    template, output = sys.argv[1:]
    lines = [
        'msgid ""',
        'msgstr ""',
        '"Project-Id-Version: reverblabr\\n"',
        '"PO-Revision-Date: 2026-01-01 00:00+0000\\n"',
        '"Last-Translator: tools/pseudo-locale.py\\n"',
        '"Language-Team: none\\n"',
        '"MIME-Version: 1.0\\n"',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Content-Transfer-Encoding: 8bit\\n"',
        '"Language: qps\\n"',
        '"Plural-Forms: nplurals=2; plural=(n != 1);\\n"',
        "",
    ]
    count = 0
    for entry in read_entries(template):
        if entry.get("msgid", "") == "":
            continue  # the template's own header
        if "msgctxt" in entry:
            lines.append(f'msgctxt "{entry["msgctxt"]}"')
        lines.append(f'msgid "{entry["msgid"]}"')
        if "msgid_plural" in entry:
            lines.append(f'msgid_plural "{entry["msgid_plural"]}"')
            lines.append(f'msgstr[0] "{pseudo(entry["msgid"])}"')
            lines.append(f'msgstr[1] "{pseudo(entry["msgid_plural"])}"')
        else:
            lines.append(f'msgstr "{pseudo(entry["msgid"])}"')
        lines.append("")
        count += 1
    with open(output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines))
    print(f"Wrote {output} with {count} pseudo-translated messages.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

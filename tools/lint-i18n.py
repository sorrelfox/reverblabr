#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Flags user-visible strings in the GUI that bypass KI18n.

Usage: tools/lint-i18n.py [--allow-list FILE] PATH...

Checks, line by line:
- QML (*.qml): the value of a user-visible property, such as text:, title:,
  placeholderText:, ToolTip.text: or Accessible.name:, must not contain a
  string literal outside an i18n call (KI18n.i18nc(...) and friends).
- C++ (*.cpp, *.h): the arguments of a user-visible setter, such as
  setText(...), setWindowTitle(...) or setToolTip(...), follow the same rule.
- Both: Qt's own translation functions (qsTr, tr) are flagged, because the
  project's catalogs come from KI18n.
- QML: a string continued on the next line with `+ "…"`. JavaScript joins the
  pieces when it runs, but xgettext reads QML as C++ and extracts only the
  first piece, so the catalog's message never matches what the window asks
  for and the words are never translated. Keep a message on one line.

Not flagged:
- literals without letters, such as "", "•" or "→"
- literals listed in the allow-list file (exact text, one per line, "#" comments)
- lines that carry the marker "no-i18n" in a comment, for example
      text: "GPU: " + name // no-i18n: debug overlay, never shown to users

Exits with status 1 when anything is flagged.
"""

import argparse
import pathlib
import re
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_ALLOW_LIST = REPO_ROOT / "tools" / "i18n-lint-allow.txt"

# QML properties whose values users see or hear, by the last part of the
# property name, plus a few full names.
QML_VISIBLE_NAMES = {
    "text", "title", "subtitle", "placeholderText", "description", "explanation",
    "toolTip", "tooltip", "label", "displayText",
}
QML_VISIBLE_PATHS = {"Accessible.name", "Accessible.description"}

CPP_VISIBLE_SETTERS = (
    "setText", "setPlainText", "setWindowTitle", "setWindowIconText", "setToolTip",
    "setStatusTip", "setWhatsThis", "setPlaceholderText", "setTitle", "setLabelText",
    "setAccessibleName", "setAccessibleDescription", "showMessage",
)

# i18n, i18nc, i18np, i18ncp, i18nd..., and the ki18n, kli18n, xi18n, kxi18n and
# klxi18n families; in QML optionally called through KI18n.
I18N_CALL = re.compile(r"(?<![\w.])(?:KI18n\s*\.\s*)?(?:k?l?x?i18n[dcp]*|I18NC?_NOOP)\s*\(")
QT_TRANSLATION_CALL = {
    ".qml": re.compile(r"(?<![\w.])(qsTr|qsTranslate|qsTrId)\s*\("),
    ".cpp": re.compile(r"(?<![\w.])(?:QObject::|QCoreApplication::)?(tr|translate)\s*\(\s*\""),
}
QML_PROPERTY = re.compile(
    r"(?:^|[{;])\s*(?:readonly\s+)?(?:(?:default|required)\s+)?(?:property\s+[\w<>.]+\s+)?"
    r"([A-Za-z_][\w.]*)\s*:(?!:)")
CPP_SETTER = re.compile(r"(?<![\w])(" + "|".join(CPP_VISIBLE_SETTERS) + r")\s*\(")
# A line that goes on with `+ "…"`: the second piece of a string split across lines.
CONTINUED_LITERAL = re.compile(r'^\s*(\+)\s*"')
LETTER = re.compile(r"[^\W\d_]")
MARKER = "no-i18n"


def scan_strings(line):
    """Returns the line with string contents blanked out (so brackets and
    colons inside strings don't count), the literals as (start, end, text),
    and the column where a // comment starts (or len(line))."""
    masked = list(line)
    literals = []
    i = 0
    while i < len(line):
        char = line[i]
        if char == "/" and line.startswith("//", i):
            return "".join(masked), literals, i
        if char in "\"'`":
            start = i
            i += 1
            while i < len(line) and line[i] != char:
                i += 2 if line[i] == "\\" else 1
            end = min(i + 1, len(line))
            literals.append((start, end, line[start + 1:end - 1]))
            for k in range(start + 1, end - 1):
                masked[k] = "_"
            i = end
            continue
        i += 1
    return "".join(masked), literals, len(line)


def closing_paren(masked, open_index, limit):
    """Index just past the parenthesis that closes the one at open_index, or
    limit if it isn't closed on this line."""
    depth = 0
    for i in range(open_index, limit):
        if masked[i] == "(":
            depth += 1
        elif masked[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    return limit


def value_end(masked, start, limit):
    """End of a QML property value: the first ; or } outside brackets."""
    depth = 0
    for i in range(start, limit):
        char = masked[i]
        if char in "([{":
            depth += 1
        elif char in ")]":
            depth -= 1
        elif char == "}":
            if depth == 0:
                return i
            depth -= 1
        elif char == ";" and depth == 0:
            return i
    return limit


def i18n_spans(masked, limit):
    spans = []
    for match in I18N_CALL.finditer(masked, 0, limit):
        open_index = match.end() - 1
        spans.append((match.start(), closing_paren(masked, open_index, limit)))
    return spans


def lint_line(line, suffix, allowed):
    """Yields (column, message) for every problem on one line."""
    if MARKER in line:
        return
    masked, literals, limit = scan_strings(line)
    covered = i18n_spans(masked, limit)

    def inside(position, spans):
        return any(start <= position < end for start, end in spans)

    def check_region(start, end, where):
        for literal_start, _, text in literals:
            if not (start <= literal_start < end) or inside(literal_start, covered):
                continue
            if not LETTER.search(text) or text in allowed:
                continue
            yield literal_start, f'bare string "{text}" in {where}; wrap it in an i18n call such as i18nc("@context", "…")'

    if suffix == ".qml":
        for match in QML_PROPERTY.finditer(masked, 0, limit):
            name = match.group(1)
            if name.split(".")[-1] not in QML_VISIBLE_NAMES and name not in QML_VISIBLE_PATHS:
                continue
            start = match.end()
            yield from check_region(start, value_end(masked, start, limit), f"{name}:")
    else:
        for match in CPP_SETTER.finditer(masked, 0, limit):
            open_index = match.end() - 1
            yield from check_region(open_index, closing_paren(masked, open_index, limit), f"{match.group(1)}()")

    for match in QT_TRANSLATION_CALL[".qml" if suffix == ".qml" else ".cpp"].finditer(masked, 0, limit):
        yield match.start(), f"{match.group(1)}() is Qt's translation system; use KI18n (i18nc) instead"


def load_allow_list(path):
    allowed = set()
    if path.exists():
        for raw in path.read_text(encoding="utf-8").splitlines():
            entry = raw.split("#", 1)[0].strip()
            if entry:
                allowed.add(entry)
    return allowed


def source_files(paths):
    for path in paths:
        if path.is_dir():
            yield from sorted(p for p in path.rglob("*") if p.suffix in (".qml", ".cpp", ".h"))
        elif path.suffix in (".qml", ".cpp", ".h"):
            yield path


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--allow-list", type=pathlib.Path, default=DEFAULT_ALLOW_LIST)
    parser.add_argument("paths", nargs="+", type=pathlib.Path)
    options = parser.parse_args()

    allowed = load_allow_list(options.allow_list)
    findings = 0
    for path in source_files(options.paths):
        suffix = ".qml" if path.suffix == ".qml" else ".cpp"
        previous = ""
        for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            for column, message in sorted(lint_line(line, suffix, allowed)):
                print(f"{path}:{number}:{column + 1}: {message}")
                findings += 1
            continued = CONTINUED_LITERAL.match(line)
            if suffix == ".qml" and continued and previous.rstrip().endswith('"') and "no-i18n" not in line:
                print(f"{path}:{number}:{continued.start(1) + 1}: string continued with +; "
                      "xgettext extracts only the first piece, so keep a message on one line")
                findings += 1
            if line.strip():
                previous = line
    sys.stdout.flush()
    if findings:
        print(f"lint-i18n: {findings} untranslated string(s). See docs/i18n.md.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

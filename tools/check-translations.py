#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Checks that the translation catalogs are safe to ship.

Usage: tools/check-translations.py [PATH...]

Reads every catalog under po/ (po/<language>/*.po), or the catalogs given and
those under the folders given, and reports each problem as FILE:LINE: message.
A message nobody has translated yet is fine, since gettext shows the English;
so are fuzzy and obsolete ones, which are never shipped. A translated message,
in each of its plural forms, must have:

- nothing gettext's own checks find (msgfmt --check): the header, the plural
  forms, and C formats where a message is marked c-format;
- the language of its folder as the header's Language, and as many forms as
  the header's Plural-Forms gives, none of them left empty;
- the same KI18n placeholders as the English (%1, %2, ...: each one kept, in
  any order), and the same %-words, the tokens of the Recorder's file names
  and of a stream's title (%session, %date, %year, %counter, %artist, %title,
  %album, and %% for a percent sign in them). The form a language keeps for
  the number 1 alone may leave out what the English singular leaves out, as
  "Im markierten Band" does for "In the marked band" / "In the %1 marked
  bands";
- the same markup tags, as many of each, with the same attributes (<b>,
  <a href="...">);
- one & accelerator where the English has one, and none where it has none.
  && is an ampersand, and so are &amp; and an & before a space, while the
  (&T) of "设置(&T)" is an accelerator. msgfmt's own --check-accelerators is
  not used: it counts every & that is not doubled, so it would want one in
  each language's "Bass & Treble", and it skips plural messages;
- no whitespace at its start or end that the English does not have.

Whether a word should have stayed English is not checked: names stay.

Exits with status 1 when anything is reported, 2 when the check cannot run.
"""

import argparse
import collections
import dataclasses
import functools
import gettext
import os
import pathlib
import re
import shutil
import subprocess
import sys

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEFAULT_PO_DIR = REPO_ROOT / "po"

# A KI18n placeholder: % and the digits after it, the first not 0 (%10 is the
# tenth). The % in front of it can be another one: "%%1" is a percent sign
# and then %1, as the Turkish "%%1" for "%1 %".
PLACEHOLDER = re.compile(r"%([1-9][0-9]*)")
# The %-words, and %%, which KI18n shows as it is. A %% before a digit is the
# percent sign in front of a placeholder.
PERCENT_WORD = re.compile(r"%%(?![1-9])|%[A-Za-z]+")
# A markup tag: <b>, </b>, <a href="...">, <br/>; not "< >" or "a < b".
TAG = re.compile(r"<(/?)([A-Za-z][A-Za-z0-9:_.-]*)((?:\s[^<>]*?)?)\s*(/?)>")
ATTRIBUTE = re.compile(r"""([^\s=/"']+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"']+)))?""")
# What an & is when it is not an accelerator: && (an ampersand, written
# twice) or the start of an entity (&amp;, &#38;). Nor is an & before a space.
NOT_ACCELERATOR = re.compile(r"&&|&(?:[A-Za-z][A-Za-z0-9]*|#[0-9]+|#[xX][0-9A-Fa-f]+);")
ACCELERATOR = re.compile(r"&(?=\S)")

KEYWORD = re.compile(r"(msgctxt|msgid_plural|msgid|msgstr)(?:\[([0-9]+)\])?\s*(.*)$")
STRING = re.compile(r'"((?:[^"\\]|\\.)*)"\s*')
ESCAPE = re.compile(r"\\(?:([0-7]{1,3})|x([0-9A-Fa-f]{1,2})|(.))", re.DOTALL)
ESCAPED = {"a": "\a", "b": "\b", "f": "\f", "n": "\n", "r": "\r", "t": "\t", "v": "\v"}

# The numbers a plural expression is tried on, to find the form a language
# keeps for 1 alone. Plural rules look at n % 10 and n % 100.
NUMBERS = range(1000)


@dataclasses.dataclass
class String:
    """One of an entry's strings, unescaped, with the line of its keyword and
    where each of its "..." pieces starts (offset in the text, line)."""

    line: int
    text: str = ""
    pieces: list = dataclasses.field(default_factory=list)

    def add(self, piece, line):
        self.pieces.append((len(self.text), line))
        self.text += piece

    def line_at(self, offset):
        return max((line for start, line in self.pieces if start <= offset), default=self.line)


@dataclasses.dataclass
class Entry:
    flags: set = dataclasses.field(default_factory=set)
    msgctxt: String = None
    msgid: String = None
    msgid_plural: String = None
    forms: list = dataclasses.field(default_factory=list)  # (n of msgstr[n], or None for msgstr; String)
    obsolete: bool = False


def unescape(match):
    octal, hexadecimal, character = match.groups()
    if octal:
        return chr(int(octal, 8))
    if hexadecimal:
        return chr(int(hexadecimal, 16))
    return ESCAPED.get(character, character)


def unquote(rest):
    """The text of the "..." strings that make up rest, unescaped, or None if
    rest is anything else."""
    pieces, position = [], 0
    while position < len(rest):
        match = STRING.match(rest, position)
        if not match:
            return None
        pieces.append(match.group(1))
        position = match.end()
    return ESCAPE.sub(unescape, "".join(pieces)) if pieces else None


def read_catalog(text):
    """The entries of a catalog, obsolete ones left out, and the numbers of
    the lines that are not part of one."""
    entries, unreadable = [], []
    entry, last = Entry(), None

    def finish():
        nonlocal entry, last
        if entry.msgid is not None and not entry.obsolete:
            entries.append(entry)
        entry, last = Entry(), None

    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            # A comment belongs to the entry after it; #~ lines are an
            # obsolete entry, which is not shipped.
            if entry.forms or (entry.obsolete and not line.startswith("#~")):
                finish()
            if line.startswith("#~"):
                entry.obsolete = True
            elif line.startswith("#,"):
                entry.flags.update(flag.strip() for flag in line[2:].split(","))
            last = None
            continue
        keyword = KEYWORD.match(line)
        value = unquote(keyword.group(3)) if keyword else None
        if value is not None:
            name, index = keyword.group(1), keyword.group(2)
            if name in ("msgctxt", "msgid") and (
                    entry.obsolete or entry.forms or entry.msgid is not None
                    or (name == "msgctxt" and entry.msgctxt is not None)):
                finish()
            last = String(number)
            last.add(value, number)
            if name == "msgctxt":
                entry.msgctxt = last
            elif name == "msgid":
                entry.msgid = last
            elif name == "msgid_plural":
                entry.msgid_plural = last
            else:
                entry.forms.append((None if index is None else int(index), last))
            continue
        value = unquote(line) if line.startswith('"') and last is not None else None
        if value is not None:
            last.add(value, number)
            continue
        unreadable.append(number)
        last = None
    finish()
    return entries, unreadable


def header_fields(header):
    """The header's fields, as name: (value, line)."""
    fields = {}
    if header is None or not header.forms:
        return fields
    string = header.forms[0][1]
    offset = 0
    for field in string.text.split("\n"):
        name, colon, value = field.partition(":")
        if colon and name.strip():
            fields[name.strip()] = (value.strip(), string.line_at(offset))
        offset += len(field) + 1
    return fields


@functools.lru_cache(maxsize=None)
def form_for_one_alone(expression):
    """The plural form the expression gives the number 1 and no other number,
    or None. A broken expression has none (msgfmt reports it)."""
    try:
        # The standard library's reader of Plural-Forms, as its own gettext
        # uses it: it refuses anything but a C expression of n.
        plural = gettext.c2py(expression)
        forms = [plural(n) for n in NUMBERS]
    except (AttributeError, ArithmeticError, RecursionError, SyntaxError, TypeError, ValueError):
        return None
    return forms[1] if forms.count(forms[1]) == 1 else None


def plural_forms(fields):
    """nplurals of the header's Plural-Forms, and the form for 1 alone (each
    None when the header doesn't say)."""
    value = fields.get("Plural-Forms", ("", 0))[0]
    count = re.search(r"\bnplurals\s*=\s*([0-9]+)", value)
    expression = re.search(r"\bplural\s*=\s*([^;]+)", value)
    return (int(count.group(1)) if count else None,
            form_for_one_alone(expression.group(1).strip()) if expression else None)


def placeholders(text):
    return {int(number) for number in PLACEHOLDER.findall(text)}


def percent_words(text):
    return set(PERCENT_WORD.findall(text))


def tags(text):
    """The markup tags of a text, each as written but with its name in lower
    case and its attributes in order of name, counted."""
    found = collections.Counter()
    for match in TAG.finditer(text):
        closing, name, attributes, empty = match.groups()
        pairs = sorted((key.lower(), quoted or single or bare)
                       for key, quoted, single, bare in ATTRIBUTE.findall(attributes))
        found["<" + closing + name.lower() + "".join(f' {key}="{value}"' for key, value in pairs) + empty + ">"] += 1
    return found


def accelerators(text):
    """How many & accelerators a text has (none inside markup tags)."""
    return len(ACCELERATOR.findall(NOT_ACCELERATOR.sub(" ", TAG.sub(" ", text))))


def edges(text):
    """The whitespace at the start and at the end of a text, without line
    breaks: whether both the English and the translation start or end with
    one is msgfmt's check."""
    start = text[:len(text) - len(text.lstrip())]
    end = text[len(text.rstrip()):] if text.strip() else ""
    return start.replace("\n", ""), end.replace("\n", "")


def show_numbers(numbers):
    return ", ".join(f"%{number}" for number in sorted(numbers))


def differences(missing, extra):
    parts = []
    if missing:
        parts.append(f"lacks {missing}, which the English has")
    if extra:
        parts.append(f"has {extra}, which the English does not")
    return "; ".join(parts)


def check_message(entry, nplurals, one_alone, problems):
    """Appends (line, message) for each problem of one entry's translation."""
    if not any(string.text for _, string in entry.forms):
        return  # not translated: gettext shows the English
    plural = entry.msgid_plural is not None
    english = [entry.msgid.text] + ([entry.msgid_plural.text] if plural else [])
    if plural and nplurals is not None and len(entry.forms) != nplurals:
        problems.append((entry.forms[0][1].line, f"has {len(entry.forms)} plural forms, "
                                                 f"but the header's Plural-Forms gives nplurals={nplurals}"))

    every_placeholder = set().union(*map(placeholders, english))
    singular_placeholders = placeholders(english[0])
    every_word = set().union(*map(percent_words, english))
    english_tags = [tags(text) for text in english]
    wants_accelerator = any(accelerators(text) for text in english)
    room_at_start = max(len(edges(text)[0]) for text in english)
    room_at_end = max(len(edges(text)[1]) for text in english)

    for index, string in entry.forms:
        text, line = string.text, string.line
        prefix = f"msgstr[{index}] " if plural and index is not None else ""
        if not text:
            problems.append((line, f"{prefix}is empty, while the message's other forms are translated"))
            continue
        alone = plural and index is not None and index == one_alone

        found = placeholders(text)
        missing = (singular_placeholders if alone else every_placeholder) - found
        extra = found - every_placeholder
        if plural and not alone and missing and not missing & singular_placeholders:
            # What the English singular leaves out, the number: only a form
            # for 1 alone may leave it out too.
            message = (f"lacks {show_numbers(missing)}, which the English plural has: in this language "
                       f"the form is not for the number 1 alone")
            if extra:
                message += f"; has {show_numbers(extra)}, which the English does not"
            problems.append((line, prefix + message))
        elif missing or extra:
            problems.append((line, prefix + differences(show_numbers(missing), show_numbers(extra))))

        words = percent_words(text)
        if words != every_word:
            problems.append((line, prefix + differences(", ".join(sorted(every_word - words)),
                                                        ", ".join(sorted(words - every_word)))))

        found_tags = tags(text)
        if found_tags not in english_tags:
            wanted = english_tags[0] if not plural or alone else english_tags[-1]
            problems.append((line, prefix + differences(", ".join(sorted((wanted - found_tags).elements())),
                                                        ", ".join(sorted((found_tags - wanted).elements())))))

        count = accelerators(text)
        if wants_accelerator and count == 0:
            problems.append((line, f"{prefix}has no & accelerator, but the English has one"))
        elif wants_accelerator and count > 1:
            problems.append((line, f"{prefix}has {count} & accelerators, but the English has one"))
        elif not wants_accelerator and count:
            problems.append((line, f"{prefix}has an & accelerator, but the English has none (&& is an ampersand)"))

        start, end = edges(text)
        if len(start) > room_at_start:
            problems.append((line, f"{prefix}starts with whitespace the English does not have"))
        if len(end) > room_at_end:
            problems.append((line, f"{prefix}ends with whitespace the English does not have"))


def msgfmt_problems(path):
    """What msgfmt --check says about a catalog, as (line, message)."""
    # Its messages in English, whatever the session speaks.
    environment = {name: value for name, value in os.environ.items() if name != "LANGUAGE"}
    environment["LC_ALL"] = "C"
    run = subprocess.run(["msgfmt", "--check", "-o", os.devnull, str(path)], capture_output=True,
                         encoding="utf-8", errors="replace", env=environment, check=False)
    problems = []
    for output_line in (run.stdout + run.stderr).splitlines():
        if not output_line.strip() or re.match(r"msgfmt: found [0-9]+ fatal error", output_line):
            continue
        if output_line[0].isspace() and problems:
            # The rest of the message before, such as its "Try using ...".
            line, message = problems[-1]
            problems[-1] = (line, f"{message} {output_line.strip()}")
            continue
        rest, line = output_line.removeprefix("msgfmt: "), 1
        if rest.startswith(f"{path}:"):
            rest = rest[len(f"{path}:"):]
            number, colon, message = rest.partition(":")
            if colon and number.isdigit():
                line, rest = int(number), message
        problems.append((line, f"msgfmt: {rest.strip()}"))
    if run.returncode != 0 and not problems:
        problems.append((1, f"msgfmt: failed with status {run.returncode}, and said nothing"))
    return problems


def check_catalog(path):
    """The problems of one catalog, as (line, message), in the order of its lines."""
    problems = msgfmt_problems(path)
    data = path.read_bytes()
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as error:
        problems.append((data[:error.start].count(b"\n") + 1, "is not UTF-8, as every catalog must be"))
        return sorted(problems, key=lambda problem: problem[0])
    entries, unreadable = read_catalog(text)
    problems += [(number, "is not part of an entry: a keyword such as msgid, a \"...\" string or a # comment")
                 for number in unreadable]

    header = next((entry for entry in entries if entry.msgctxt is None and entry.msgid.text == ""), None)
    fields = header_fields(header)
    language = path.parent.name
    header_line = header.forms[0][1].line if header is not None and header.forms else 1
    value, line = fields.get("Language", (None, header_line))
    if value is None:
        problems.append((line, f'the header has no Language; the catalog is in the folder "{language}"'))
    elif value != language:
        problems.append((line, f'the header says Language: {value}, but the catalog is in the folder "{language}"'))

    nplurals, one_alone = plural_forms(fields)
    for entry in entries:
        if entry is not header and "fuzzy" not in entry.flags:
            check_message(entry, nplurals, one_alone, problems)
    return sorted(problems, key=lambda problem: problem[0])


def catalogs(paths):
    for path in paths:
        if path.is_dir():
            yield from sorted(path.rglob("*.po"))
        else:
            yield path


def main():
    # The help is this file's description, which says what each check asks for.
    parser = argparse.ArgumentParser(
        description="\n\n".join(part for part in __doc__.split("\n\n") if not part.startswith("Usage:")),
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="*", type=pathlib.Path,
                        help="catalogs, and folders to read every *.po under (default: po/)")
    options = parser.parse_args()
    paths = options.paths or [DEFAULT_PO_DIR]
    for path in paths:
        if not path.exists():
            parser.error(f"{path}: no such file or folder")
    if shutil.which("msgfmt") is None:
        print("check-translations: msgfmt is needed; it comes with GNU gettext", file=sys.stderr)
        return 2

    files = list(catalogs(paths))
    problems = faulty = 0
    for path in files:
        found = check_catalog(path)
        for line, message in found:
            print(f"{path}:{line}: {message}")
        problems += len(found)
        faulty += bool(found)
    sys.stdout.flush()
    if problems:
        print(f"check-translations: {problems} problem(s) in {faulty} of {len(files)} catalog(s); "
              "--help says what each check asks for", file=sys.stderr)
        return 1
    languages = len({path.parent.name for path in files})
    print(f"check-translations: {len(files)} catalog(s) in {languages} language(s), nothing to report"
          if files else "check-translations: no catalogs", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

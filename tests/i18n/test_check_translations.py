#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Self-test for tools/check-translations.py.

Writes small catalogs to a temporary folder and runs the check on them. The
correct ones hold what a translation may rightly do: an accelerator written
"設定(&T)", "Bass & Treble" without an &, the placeholders in another order,
a singular without its number where the language keeps that form for 1
alone; they must pass with nothing reported. Each of the others has one
fault, which must be reported on its line, and nothing else may be.
"""

import collections
import pathlib
import re
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
CHECK = HERE.parent.parent / "tools" / "check-translations.py"
FINDING = re.compile(r"^(?P<path>.+?):(?P<line>\d+): (?P<message>.*)$")

PLURAL_FORMS = {
    "de": "nplurals=2; plural=(n != 1);",
    "fr": "nplurals=2; plural=(n > 1);",
    "tr": "nplurals=2; plural=(n != 1);",
    "ru": "nplurals=3; plural=(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2);",
    "ja": "nplurals=1; plural=0;",
}

HEADER = r'''msgid ""
msgstr ""
"Project-Id-Version: reverblabr\n"
"PO-Revision-Date: 2026-11-07 00:00+0000\n"
"Last-Translator: Reverblabr contributors\n"
"Language-Team: none\n"
"Language: {language}\n"
"MIME-Version: 1.0\n"
"Content-Type: text/plain; charset=UTF-8\n"
"Content-Transfer-Encoding: 8bit\n"
"Plural-Forms: {plural}\n"

'''

GOOD = {
    "de": r'''
#. A name, and its & is "and": not an accelerator, so the translation needs
#. none, though msgfmt --check-accelerators would want one.
#: app/blockpresentation.cpp:184
msgctxt "@item block name"
msgid "Bass & Treble"
msgstr "Bass und Höhen"

msgctxt "@action:inmenu"
msgid "&Open…"
msgstr "Ö&ffnen …"

msgctxt "@action:button"
msgid "Copy && Paste"
msgstr "Kopieren und Einfügen"

msgctxt "@info"
msgid "Recording %1 in %2"
msgstr "In %2 wird %1 aufgenommen"

msgctxt "@info a percentage"
msgid "%1 %"
msgstr "%1 %"

msgid "Double volume (200 %)"
msgstr "Doppelte Lautstärke (200 %)"

#. The singular has no number, and German keeps its form for 1 alone.
msgid "In the marked band more gain is unlikely to help."
msgid_plural "In the %1 marked bands more gain is unlikely to help."
msgstr[0] "Im markierten Band hilft mehr Verstärkung kaum."
msgstr[1] "In den %1 markierten Bändern hilft mehr Verstärkung kaum."

msgctxt "@info a plugin scan left some out; the link tries them again"
msgid "%1 plugin could not be read. <a href=\"retry\">Try again</a>"
msgid_plural "%1 plugins could not be read. <a href=\"retry\">Try again</a>"
msgstr[0] ""
"%1 Plugin konnte nicht gelesen werden. <a href=\"retry\">Erneut versuchen</a>"
msgstr[1] ""
"%1 Plugins konnten nicht gelesen werden. <a href=\"retry\">Erneut "
"versuchen</a>"

msgid "<b>Keep it comfortable.</b> Sound should never hurt."
msgstr "<b>Angenehm bleiben.</b> Ton sollte nie wehtun."

msgctxt "@info a reason a preset was refused"
msgid "a name can't contain / \\ : * ? \" < > |"
msgstr "ein Name kann kein / \\ : * ? \" < > | enthalten"

msgctxt "@info between the items of a list"
msgid ", "
msgstr ", "

msgctxt "@info between the last two items of a list"
msgid " and "
msgstr " und "

msgctxt "@info:usage"
msgid ""
"Usage: reverblabr-ctl COMMAND\n"
"\n"
"  list    the sessions' names\n"
msgstr ""
"Aufruf: reverblabr-ctl BEFEHL\n"
"\n"
"  list    die Namen der Sitzungen\n"

#, c-format
msgctxt "@info help page of the Broadcast block"
msgid "A new block uses %artist - %title."
msgstr "Ein neuer Block nimmt %artist - %title."

#, c-format
msgid "%%: a percent sign."
msgstr "%%: ein Prozentzeichen."

msgctxt "@label not translated yet"
msgid "Level"
msgstr ""

msgid "%1 file"
msgid_plural "%1 files"
msgstr[0] ""
msgstr[1] ""

#. Fuzzy and obsolete entries are not shipped, so their faults don't count.
#, fuzzy
#| msgid "Open"
msgid "&Open"
msgstr " Öffnen %3"

#~ msgctxt "@label"
#~ msgid "Gain"
#~ msgstr " Verstärkung %1 &&"
''',
    "ja": r'''
msgctxt "@item block name"
msgid "Bass & Treble"
msgstr "低音・高音"

msgctxt "@title:menu"
msgid "Se&ttings"
msgstr "設定(&T)"

msgctxt "@info between the items of a list"
msgid ", "
msgstr "、"

msgctxt "@info between the last two items of a list"
msgid " and "
msgstr "と"

#. One form for every number, so it keeps the number.
msgid "In the marked band more gain is unlikely to help."
msgid_plural "In the %1 marked bands more gain is unlikely to help."
msgstr[0] "マークした %1 個の帯域では、ゲインを上げてもあまり効果はありません。"

msgctxt "@info"
msgid "Recording %1 in %2"
msgstr "%2 で %1 を録音中"
''',
    "ru": r'''
msgid "%1 file"
msgid_plural "%1 files"
msgstr[0] "%1 файл"
msgstr[1] "%1 файла"
msgstr[2] "%1 файлов"

#. The first form is also for 21, 31, ..., so it keeps the number.
msgid "In the marked band more gain is unlikely to help."
msgid_plural "In the %1 marked bands more gain is unlikely to help."
msgstr[0] "В %1 отмеченной полосе большее усиление вряд ли поможет."
msgstr[1] "В %1 отмеченных полосах большее усиление вряд ли поможет."
msgstr[2] "В %1 отмеченных полосах большее усиление вряд ли поможет."
''',
    "tr": r'''
#. The percent sign goes before the number: "%%1" is % and then %1.
msgctxt "@info a percentage"
msgid "%1 %"
msgstr "%%1"

msgctxt "@label:slider"
msgid "Volume: %1 %"
msgstr "Ses düzeyi: %%1"
''',
}

# expected: (the line, by text on it or its number; what the finding says).
# "msgfmt:" is a finding of gettext's own, which may be on any line of the
# entry, as the version of gettext puts it. msgfmt_too: gettext reports the
# fault as well, in its own words.
Fault = collections.namedtuple("Fault", "name language body expected header_language plural msgfmt_too",
                               defaults=(None, None, False))

FAULTS = [
    # gettext's own checks, which the check passes on.
    Fault("msgfmt-newline", "de", r'''
msgctxt "@info:usage"
msgid "Usage: reverblabr-ctl COMMAND\n"
msgstr "Aufruf: reverblabr-ctl BEFEHL"
''', [('msgstr "Aufruf', "msgfmt:")]),
    Fault("msgfmt-c-format", "de", r'''
#, c-format
msgid "%%: a percent sign."
msgstr "%: ein Prozentzeichen."
''', [('msgstr "%: ein', "msgfmt:"), ('msgstr "%: ein', "lacks %%")]),
    Fault("msgfmt-plural-expression", "de", r'''
msgid "%1 file"
msgid_plural "%1 files"
msgstr[0] "%1 Datei"
msgstr[1] "%1 Dateien"
''', [("Plural-Forms:", "msgfmt:")], plural="nplurals=2; plural=(n != 1;"),
    # The header.
    Fault("language", "de", r'''
msgid "Level"
msgstr "Pegel"
''', [("Language: de_DE", "Language")], header_language="de_DE"),
    Fault("plural-forms", "ru", r'''
msgid "%1 file"
msgid_plural "%1 files"
msgstr[0] "%1 файл"
msgstr[1] "%1 файла"
''', [('msgstr[0] "%1 файл"', "nplurals=3")], msgfmt_too=True),
    Fault("plural-form-empty", "ru", r'''
msgid "%1 file"
msgid_plural "%1 files"
msgstr[0] "%1 файл"
msgstr[1] ""
msgstr[2] "%1 файлов"
''', [('msgstr[1] ""', "empty")]),
    # Placeholders and %-words.
    Fault("placeholder-lost", "de", r'''
msgctxt "@info"
msgid "Recording %1 in %2"
msgstr "Aufnahme von %1"
''', [('msgstr "Aufnahme', "lacks %2")]),
    Fault("placeholder-added", "de", r'''
msgctxt "@label"
msgid "Level"
msgstr "Pegel %1"
''', [('msgstr "Pegel %1"', "has %1")]),
    # The first form is also for 21, 31, ...: it needs the number.
    Fault("placeholder-number", "ru", r'''
msgid "In the marked band more gain is unlikely to help."
msgid_plural "In the %1 marked bands more gain is unlikely to help."
msgstr[0] "В отмеченной полосе большее усиление вряд ли поможет."
msgstr[1] "В %1 отмеченных полосах большее усиление вряд ли поможет."
msgstr[2] "В %1 отмеченных полосах большее усиление вряд ли поможет."
''', [('msgstr[0] "В отмеченной', "lacks %1")]),
    # The first form is also for 0.
    Fault("placeholder-number-zero", "fr", r'''
msgid "In the marked band more gain is unlikely to help."
msgid_plural "In the %1 marked bands more gain is unlikely to help."
msgstr[0] "Dans la bande marquée, plus de gain n'aidera guère."
msgstr[1] "Dans les %1 bandes marquées, plus de gain n'aidera guère."
''', [('msgstr[0] "Dans la', "lacks %1")]),
    Fault("percent-word", "de", r'''
msgctxt "@info the tokens of a file name"
msgid "%session %year"
msgstr "%Sitzung %year"
''', [('msgstr "%Sitzung', "lacks %session")]),
    # KI18n shows %% as two percent signs.
    Fault("percent-sign", "de", r'''
msgid "Double volume (200 %)"
msgstr "Doppelte Lautstärke (200 %%)"
''', [('msgstr "Doppelte', "has %%")]),
    # Markup.
    Fault("tag", "de", r'''
msgid "<b>Keep it comfortable.</b> Sound should never hurt."
msgstr "<b>Angenehm bleiben.<b> Ton sollte nie wehtun."
''', [('msgstr "<b>Angenehm', "lacks </b>")]),
    Fault("href", "de", r'''
msgid "Some plugins may be missing. <a href=\"again\">Look again</a>"
msgstr "Einige Plugins fehlen vielleicht. <a href=\"nochmal\">Erneut suchen</a>"
''', [('msgstr "Einige', 'lacks <a href="again">')]),
    # Accelerators.
    Fault("accelerator-lost", "de", r'''
msgctxt "@action:inmenu"
msgid "&Open…"
msgstr "Öffnen …"
''', [('msgstr "Öffnen', "no & accelerator")]),
    Fault("accelerator-added", "de", r'''
msgctxt "@label"
msgid "Open"
msgstr "Ö&ffnen"
''', [('msgstr "Ö&ffnen"', "has an & accelerator")]),
    Fault("accelerator-twice", "ja", r'''
msgctxt "@title:menu"
msgid "Se&ttings"
msgstr "設定(&S)(&T)"
''', [('msgstr "設定', "2 & accelerators")]),
    # Whitespace.
    Fault("whitespace-start", "de", r'''
msgid "Level"
msgstr " Pegel"
''', [('msgstr " Pegel"', "starts with whitespace")]),
    Fault("whitespace-end", "de", r'''
msgid "Level"
msgstr "Pegel "
''', [('msgstr "Pegel "', "ends with whitespace")]),
]


def write_catalog(path, language, body, header_language=None, plural=None):
    """Writes a catalog: a header for its folder's language (or the one
    given), then body."""
    path.parent.mkdir(parents=True)
    header = HEADER.format(language=header_language or language, plural=plural or PLURAL_FORMS[language])
    path.write_text(header + body.lstrip("\n"), encoding="utf-8")
    return path


def run_check(*paths):
    """The check's exit status, its findings as (path, line, message), and
    everything it printed."""
    result = subprocess.run([sys.executable, str(CHECK), *map(str, paths)], capture_output=True, text=True)
    findings = []
    for output_line in result.stdout.splitlines():
        match = FINDING.match(output_line)
        if not match:
            raise AssertionError(f"unexpected output of the check: {output_line}")
        findings.append((match["path"], int(match["line"]), match["message"]))
    return result.returncode, findings, result.stdout + result.stderr


def line_with(lines, text):
    for number, line in enumerate(lines, start=1):
        if text in line:
            return number
    raise AssertionError(f"no line of the catalog has {text!r}")


def entry_lines(lines, number):
    """The numbers of the lines of the entry that line `number` is in."""
    first = last = number
    while first > 1 and lines[first - 2].strip():
        first -= 1
    while last < len(lines) and lines[last].strip():
        last += 1
    return range(first, last + 1)


def unmatched(fault, path, findings):
    """What is wrong with the findings on one faulty catalog: one that is
    expected and missing, or one too many."""
    lines = path.read_text(encoding="utf-8").splitlines()
    left = [(line, message) for _, line, message in findings]
    failures = []
    for where, says in fault.expected:
        number = where if isinstance(where, int) else line_with(lines, where)
        accepted = entry_lines(lines, number) if says == "msgfmt:" else (number,)
        found = next((finding for finding in left if finding[0] in accepted and says in finding[1]), None)
        if found is None:
            failures.append(f"{fault.name}: nothing on line {number} says {says!r}")
        else:
            left.remove(found)
    for line, message in left:
        if not (fault.msgfmt_too and message.startswith("msgfmt:")):
            failures.append(f"{fault.name}: line {line}, not expected: {message}")
    return failures


def main():
    failures = []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        good = [write_catalog(scratch / "good" / language / "reverblabr.po", language, body)
                for language, body in GOOD.items()]
        status, findings, output = run_check(scratch / "good")
        if status != 0 or findings:
            failures.append(f"the correct catalogs: expected no findings, got:\n{output}")
        # A catalog named on the command line rather than found in a folder.
        status, findings, output = run_check(good[0])
        if status != 0 or findings:
            failures.append(f"{good[0]} named: expected no findings, got:\n{output}")

        faulty = set()
        for fault in FAULTS:
            path = write_catalog(scratch / "faults" / fault.name / fault.language / "reverblabr.po",
                                 fault.language, fault.body, fault.header_language, fault.plural)
            faulty.add(path)
            status, findings, output = run_check(path.parent.parent)
            if status != 1:
                failures.append(f"{fault.name}: expected exit status 1, got {status}\n{output}")
            failures += unmatched(fault, path, findings)

        # Every catalog under a folder, as the build's test reads po/: only
        # the faulty ones are reported.
        status, findings, output = run_check(scratch)
        reported = {pathlib.Path(path) for path, _, _ in findings}
        if status != 1 or reported != faulty:
            failures.append(f"the whole folder: status {status}; reported as well: "
                            f"{sorted(map(str, reported - faulty))}; missed: {sorted(map(str, faulty - reported))}")
        # No catalogs yet: nothing to report.
        (scratch / "empty").mkdir()
        status, findings, output = run_check(scratch / "empty")
        if status != 0 or findings:
            failures.append(f"an empty folder: expected status 0 and no findings, got {status}:\n{output}")

    for failure in failures:
        print("FAIL:", failure)
    if failures:
        return 1
    print(f"PASS: check-translations self-test ({len(FAULTS)} faults caught, {len(GOOD)} correct catalogs pass)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

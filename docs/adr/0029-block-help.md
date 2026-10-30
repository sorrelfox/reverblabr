<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0029: A help page for every block, written as QML data with one KI18n message a paragraph

- **Status:** Proposed
- **Date:** 2026-10-30

## Context

A block's settings card says what each control is called, and a tooltip says a little more. That is not enough for somebody who does not know what an RMS level is, what overdrive does to a slider, or why a recording would split itself on silence. Each block needs a page that explains, in plain words, what it is for, how it works and what each of its settings does, in a window that can stay open beside the card while the settings are used.

The text has to be translatable from the start, by translators who work with gettext catalogues, as the rest of the interface is (docs/i18n.md):

- **One message a paragraph or list item**, with a context, so a translator works through a page in pieces and a sentence changed in English unsettles one message, not the page.
- **A setting's name on the page is the panel's name for it**, in every language, or the page and the card disagree the moment one of them is translated.
- **No second copy of the same text** to keep in step by hand.

There were three places such text could live:

1. **The handbook**, doc/index.docbook, whose block sections (`block-<typeid>`) KDE's documentation translation already covers. Each is one short paragraph today. The handbook is built by KDocTools, which is optional (the build works without it), and it is translated in a catalogue of its own, a whole `<para>` at a time, by KDE's documentation teams. Reading it from inside the application would mean shipping and parsing the generated HTML or the DocBook, in the language KDocTools happened to build.
2. **A data file per block** (JSON), read at run time, with a script of our own to pull its strings into the catalogue.
3. **QML files of data**, one per block, whose strings are KI18n calls where they are written.

## Decision

**A block's help is a QML file of data**, `app/qml/<Block>Help.qml`: a `HelpPage` with the block's type id, a one-line summary, and a list of `HelpSection`s. A section is a heading and its paragraphs, a list of points, and the paragraphs that close it; a section marked `setting` is one setting, drawn under the part before it. **Every paragraph, point and heading is one `KI18n.i18nc()` call on a line of its own**, with the context `@info help page of the <Block> block`, and translator notes as `// i18n:` comments above the line. The existing extraction (Messages.sh, tools/extract-messages.sh) picks them up with no change, into the `reverblabr` catalogue the interface is translated in.

**A setting's heading is the panel's own message**: the same context and the same words as the panel's label, so gettext has one entry for both and the page names the setting exactly as the card does, in every language. The parts every page has ("What it does", "Settings", "When to use it", "Tips") are named once, in `BlockHelp`.

**`BlockHelp`**, a singleton, files the pages by type id and owns **one help window**, made the first time somebody presses a help button and then kept: asking about another block turns its page. `BlockHelpWindow` is a top-level window of its own (no transient parent), asked to stay above the others, and draws every page the same way from the design tokens: a column of at most `AppMetrics.helpMeasure` (66) characters, the help fonts in `AppFonts`, the spacing in `AppMetrics`, both appearances and the desktop's scheme. A block with no page yet gets the window too, with the library's description of it and a button to the handbook.

**The handbook keeps its short section per block**, an overview of a few lines that points to the help window for the details; it is not generated from the help, and the help does not repeat it word for word. `tools.handbook-blocks` goes on checking that every block has its section. **Two checks keep the help in step**:

- `tools.help-pages`: every block of the library has a page or is listed in `tests/tools/help-pending.txt`, never both; every page is filed under the block it describes; every line of text in a page is one marked message.
- `tst_blockhelp.qml`: each page, against its block's real panel, made on the mock engine: every control a person can reach in the panel is named by a heading of the page, or by the names a section says it covers (a control's screen-reader name where it differs from its label, the choices of a row).

How to write a page is in [docs/dev/block-help.md](../dev/block-help.md).

## Consequences

- Translators get the help in the catalogue they already work in, a paragraph at a time, and a setting's name once.
- The help is in the application and in the same language as its interface, whether or not KDocTools was there at build time, and it opens offline.
- A page that forgets a setting, or a panel that gains one its page does not explain, fails a test; so does a block added without a page or a line in the pending list.
- The pages are QML, so they are compiled into the application and loaded only when shown. They hold no layout, so changing how help looks is one file.
- The handbook and the help are two texts about the same blocks. They are kept different in kind (an overview, and the full explanation) so that neither is a copy to update by hand when the other changes; a fact that changes may still have to be changed in both.
- Every card is one button wider (`AppMetrics.settingsButtonRoom`), added by the card rather than asked of each panel, which moves the panel states of the design comparison.
- Most blocks have no page yet; until they do, their button shows the placeholder page.

## Alternatives considered

- **The handbook's block sections as the source.** One text and a translation workflow that exists, but the build cannot rely on KDocTools, the sections would have to grow to several pages each inside a handbook meant to be read through, a `<para>` is translated as one unit with its markup, and a setting's name in the handbook is a separate message from the panel's, free to drift in translation. Rejected.
- **JSON per block, with an extractor of our own.** Data as plain as the QML, but a second extraction path to maintain beside Messages.sh, strings that a lint cannot see as KI18n calls, and a loader to write and test. The QML costs nothing extra and is checked by qmllint. Rejected.
- **One message per page, as rich text.** Easiest to write and worst to translate: a typo fixed in one sentence would send the whole page back to every translator. Rejected.
- **A dialog in the main window.** It would cover the card it explains and close with the main window. Rejected for a window of its own.

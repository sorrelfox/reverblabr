<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Writing a block's help page

Every block's settings card has a help button beside its ⋮ menu. It opens the block's help page in a window of its own. This guide says how to write a page: where it goes, what it says, in what voice, and how to check that what it says is true. Why the help is built this way is in [ADR-0029](../adr/0029-block-help.md).

Three pages are written and serve as examples: `app/qml/VolumeHelp.qml` (a simple block), `app/qml/PeakRmsHelp.qml` (a meter) and `app/qml/RecorderHelp.qml` (a block with tabs and many settings). The blocks still without a page are listed in `tests/tools/help-pending.txt`.

## The steps

1. Read the block's panel (`app/qml/<Block>Panel.qml`, or the generic panel's rows if it has none), its engine side (`engine/blocks/<block>.h` and `.cpp`) and its section in the handbook (`doc/index.docbook`, `block-<typeid>`).
2. Write `app/qml/<Block>Help.qml`, a `HelpPage`, as below.
3. Add it to `app/qml/BlockHelp.qml`: a `Component` property, and its line in `pages`.
4. Add the file to the QML sources in `app/CMakeLists.txt`.
5. Take the block's line out of `tests/tools/help-pending.txt`.
6. Run the checks (below), and look at the page in the window, in both appearances.

The handbook's section stays a short overview. Do not copy the page into it.

## The structure

```qml
HelpPage {
    typeId: "effect.volume"
    summary: KI18n.i18nc("@info help page of the Volume block", "One or two sentences: what the block is for.")

    HelpSection {
        title: BlockHelp.aboutTitle          // What it does
        paragraphs: [ … ]
    }
    HelpSection {
        title: BlockHelp.settingsTitle       // Settings
    }
    HelpSection {
        setting: true
        title: KI18n.i18nc("@label:slider", "Volume")   // the panel's own message
        paragraphs: [ … ]
    }
    // … a section for every setting, in the panel's order …
    HelpSection {
        title: BlockHelp.whenTitle           // When to use it
        points: [ … ]
    }
    HelpSection {
        title: BlockHelp.tipsTitle           // Tips
        points: [ … ]
    }
}
```

- **The parts, in this order**: the summary under the title; *What it does*; *Settings*, with a section per setting; *When to use it*; *Tips*. A panel with tabs gets a part per tab, named as the tab is (`RecorderHelp.qml`: General, Tags, Limits), instead of one *Settings* part. A block with buttons that act rather than set (Pause, Split file) gets a part of its own for them.
- **A section** has `paragraphs`, then `points` (a list), then `closing` (paragraphs after the list). Use what you need.
- **A setting's `title` is the panel's own message**: copy the panel's `KI18n.i18nc()` call for the label, with its context, exactly. Leave out a trailing colon (the panel's screen-reader name usually has none; use that message). The heading is then the same entry in the catalogue as the label, and the same word in every language.
- **`covers`** lists the other names the panel gives the controls a section explains, each as the panel's own message: a screen-reader name that differs from the row's label ("Silence threshold" under the label "Silence below"), the choices of a segmented row ("Stereo", "Mono"), a button whose words change ("Resume"), a tool button beside a field ("Choose Folder…").
- **Every paragraph, point, heading and name is one `KI18n.i18nc()` call on a line of its own**, with nothing joined to it. Use the context `@info help page of the <Block> block`; add a few words after it for a list item that needs them (`, one choice of Hold peaks`). A note for translators goes in a `// i18n:` comment on the line above: say which words must stay as they are (`%counter`), and what an abbreviation stands for.
- No layout, no colours, no fonts: the window draws every page the same way.

## The voice

Write for somebody who has never used an audio program and does not want to learn its words.

- **Short sentences.** One idea each. Prefer "it" and "you" to the passive.
- **Say what it does to the sound, or to the file**, then how to use it. Not how it is built.
- **Explain a term the first time it appears, or avoid it.** "Full scale is the loudest a digital signal can be." "RMS is short for root mean square, a way of averaging that follows how loud the sound feels." A unit is written out the first time: "kilobits a second (kbps)".
- **Give the numbers a person can use**: the range of a setting, its unit, and what a new block starts at. Leave out the ones only a developer needs (sample counts, buffer sizes).
- **When to use it** is a short list of situations, in the words of the person in them: "One program is much louder than another."
- **Tips** are things to do, each a sentence or two: where to put a meter, which choice to start with, what to try when something sounds wrong.
- **Our own words only.** Do not borrow text, names or pictures from another product's manual or help, and do not compare the block with another product.
- **No em-dashes**, as everywhere in the documentation. Use a colon, a comma or two sentences.
- Write `100 %` with a space, as the interface does, and `−` (the minus sign) for a negative number.

## Checking the facts

Every statement on a page must be true of the code as it is. For each setting, look up:

| What | Where |
|---|---|
| Its range, its default, its choices | The block's `makeDescriptors()` in `engine/blocks/<block>.cpp`, or the catalogue (`BlockCatalogue.params(typeId)` in a QML test) |
| What the panel calls it, and the choices' words | `app/qml/<Block>Panel.qml` and its `<Block>Words.qml` |
| What it does to the sound | The block's `process()` and the header's comments; constants such as smoothing times and ceilings are in the header |
| What the interface does with it (meters, holds, the tray) | The C++ behind the panel in `app/` (for example `app/meterhub.h`, `app/peakhold.cpp`) |
| What the user documentation already says | `doc/index.docbook` and `docs/*.md`; when they disagree with the code, the code is right, and the documentation needs fixing too |

Name the source of the page's facts in the comment at the top of the file, so the next person knows where to look when the block changes. When you cannot find a fact in the code, leave the statement out.

## The checks

```sh
cmake --build build/debug
ctest --test-dir build/debug -R 'tools.help-pages|qml.mainwindow|i18n'
```

- `tools.help-pages` fails when a block has neither a page nor a line in the pending list, when a block has both, when a page is filed under the wrong block, or when a line of a page's text is not one marked message.
- `tst_blockhelp.qml` (in `qml.mainwindow`) makes the block's real panel on the mock engine and fails for every control a person can reach that the page names nowhere, by a heading or in `covers`. When a panel gains a setting, its page must gain a section.
- `i18n.pseudo-locale.*` renders every written page in the made-up language, about 40 % longer, and fails on text that is untranslated or cut off. Its screenshots are in `build/debug/tests/i18n/pseudo-*/help-<typeid>.png`.
- To look at a page in both appearances, open the block's settings in the application and press its help button, or grab it offscreen with the design grabber: a screenshot in a scene file with `"help": "<typeid>"` (see `tests/design/README.md`).

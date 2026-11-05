<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0035: One folder per block, with its C++, its interface, its icon, its presets and its tests; a block takes nothing from another

- **Status:** Accepted
- **Date:** 2026-11-05

## Context

A session is built from blocks: there are 60 block type ids, and more are coming. Until now a block's files were spread by layer:

- its C++ in `engine/blocks/` (123 files);
- its face, panel, help page and words in `app/qml/`, where they were about two thirds of the 314 files;
- its icon in `app/icons/`, its presets in `engine/presets/<typeId>/`;
- its tests in `tests/unit/blocks/` and `tests/qml/`.

The commit that added Sudden Sounds touched 29 files in 12 folders. 16 of them were shared files, and 8 of those were bookkeeping: three build lists (in `app/`, `engine/blocks/` and `tests/unit/`) and five tables (`builtinblocks.cpp`, `blockpresentation.cpp`, `BlockFaces.qml`, `BlockPanels.qml` and the pages of `BlockHelp.qml`). Nothing showed which files made up a block, and nothing stopped one block from using another's code: Night Level and Voice Gauge called Speech Now for its detector reading.

The layers themselves are sound, and checked: the block code is plain C++23 with no Qt, KDE Frameworks or PipeWire (`rb_check_layering`), which is what lets the daemon run with no interface in it.

## Decision

**A block's files are in one folder, `blocks/<folder>/`**, named as its C++ files are:

```
blocks/reverb/
  CMakeLists.txt            rb_add_block(reverb ENGINE … UI … ICONS … PRESETS … TESTS … QML_TESTS …)
  reverb.h, reverb.cpp      the block, built into reverblabr-blocks
  ui/                       its face, panel, help page, words and entry, its icon, and C++ only it uses
  presets/effect.reverb/    its factory presets, by type id
  tests/                    test_reverb.cpp, tst_reverb.qml
```

- **Blocks that share C++ share a folder**: `cutofffilter` (Low Pass and High Pass), `frequencymove` (Frequency Lowering and Frequency Raising), `broadcast` (Broadcast and Live Stream), `timeshift` (with its spill), `modelblock` (with its host link). One class may serve several type ids, as Hearing Compensation 16 and 64 or the five plugin formats do. What several unrelated blocks use and is not a block is in `blocks/shared/`, such as the voice detector and the word its reading is. Interface pieces several blocks use stay in `app/qml/`.
- **The build targets keep the layers apart, not the folders.** `rb_add_block()` (`cmake/RbBlocks.cmake`) builds a block's C++ into `reverblabr-blocks`, which the layering check holds to plain C++23, and its interface into the QML module. A QML file keeps its name as its type name, an icon stays at `icons/<typeId>.svg` in the module and a preset at `/io/github/reverblabr/presets/<typeId>/`, so nothing that names them changes.
- **A block's folder lists its own files, one by one**, as the presets were listed before: adding one is still a change somebody reviews.
- **The checklist is in the function**: a block without an icon, an entry, a help page or a test does not configure, and the message says which is missing. `REQUIRES` leaves an optional block's C++ and its GoogleTest sources out; its interface, its presets and its QML tests are built all the same, as the interface names every block whether or not the build has it.
- **What the interface shows of a block is in its entry**, `ui/<Name>Ui.qml`: its type ids, its face, its panel and its help page. `BlockRegister` lists the entries, and replaces `BlockFaces`, `BlockPanels` and the pages of `BlockHelp`.
- **A block takes nothing from another block.** Its C++ includes its own folder's headers, `blocks/shared`'s and the engine's; its QML uses its own types and the interface's. A block's tests may use other blocks, as a session does. `tools.block-folders` checks this for the block folders, that every file in a folder is named in its `CMakeLists.txt`, and that every entry is in `BlockRegister`. The engine and the interface may still know a block: the mock engine fakes some blocks' readings, and some of the interface's shared pieces name a block.
- **The tables that span blocks stay where they are**: `blocks/builtinblocks.cpp` registers each block in one line, and `app/blockpresentation.cpp` holds the library's names, descriptions and categories. The documentation for people using Reverblabr stays organised for them, and the tests that span blocks stay in `tests/`.

## Consequences

- A new block is a folder, a line in `blocks/CMakeLists.txt`, a line in `builtinblocks.cpp`, an entry in `blockpresentation.cpp` and a line in `BlockRegister.qml`, besides its documentation. Sudden Sounds' 8 bookkeeping edits would have been 4.
- A block's history and its review are, for the most part, one folder.
- Includes name the folder, `#include "blocks/reverb/reverb.h"`: the top of the tree is on the include path of the blocks and the engine. A block's interface C++ is included by its name alone, as the QML type registration includes it.
- The QML test runner reads a folder of links to `tests/qml/` and to every block's `tests/`, made at configure time, and its tests still run as `qml.mainwindow`. A block's GoogleTest sources go into `reverblabr-blocks-test`, its interface C++ tests into `reverblabr-app-test` and the Model block's into `reverblabr-model-test`, so every test keeps its name.
- `Messages.sh` extracts each block's `ui/`, and the i18n lint reads it. A block's code outside `ui/` says nothing to a person, as the engine never does, and `i18n.extract` checks that.
- The records before this one name the old paths: they describe their time.

## Alternatives considered

- **A folder per block inside each layer** (`engine/blocks/reverb/`, `app/qml/blocks/reverb/`, `tests/unit/blocks/reverb/`): it keeps the layout most KDE applications have, but a block would still live in four places.
- **Folders by category** (`blocks/effects/reverb/`): categories are the library's decision and change. The type id prefixes are permanent, but they do not follow the code: one class can register several ids.
- **A library or a plugin for each block**: a static library drops code that registers itself unless it is linked whole, and built-in blocks gain nothing from being loaded at run time.
- **Generating `builtinblocks.cpp` and the register from the folders**: it saves one line a block, and hides where a block is registered.

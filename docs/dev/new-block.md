<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# Adding a block

Everything of a block is in its folder, `blocks/<folder>/`, and a few shared lists name it ([ADR-0035](../adr/0035-block-folders.md)). The folder is named as the block's C++ files are, in lower case. Blocks that share C++ share a folder; what several unrelated blocks use goes into `blocks/shared/`, never into one of them.

## The folder

| File | What it is |
|---|---|
| `<block>.h`, `<block>.cpp` | The block: an `rb::dsp::Block` (`engine/dsp/block.h`) with its type id, its `make()` and its parameter descriptors, which give it its presets, its session file entry and the generic face and panel. Plain C++23: no Qt, no KDE Frameworks, no PipeWire. |
| `ui/<Name>Ui.qml` | Its entry in the interface: its type ids, and its face, panel and help page. One it leaves out is the generic one. |
| `ui/<Name>Help.qml` | Its help page ([Writing a block's help page](block-help.md)). |
| `ui/<typeId>.svg` | Its icon: a single-colour mask, which the theme and the category's tint colour. |
| `ui/<Name>Face.qml`, `ui/<Name>Panel.qml`, `ui/<Name>Words.qml` | Its own face, panel and words, where the generic ones are not enough, and any C++ only its interface uses. |
| `presets/<typeId>/NN-name.json` | Its factory presets, if it has any. |
| `tests/test_<block>.cpp` | Its GoogleTest cases, against a signal through the block; in `reverblabr-blocks-test` they run as `blocks.<Suite>.<Test>`. |
| `tests/tst_<block>.qml` | Its Qt Quick tests, where it has a face or a panel of its own. |
| `CMakeLists.txt` | `rb_add_block()`, naming each of these by role, one by one: `cmake/RbBlocks.cmake` says how. A block that needs a build option says so with `REQUIRES`. |

## The shared lists

1. `blocks/CMakeLists.txt`: its `add_subdirectory()`.
2. `blocks/builtinblocks.cpp`: its line in the registry.
3. `app/blockpresentation.cpp`: its entry for the library, with its category, its name, its one line and its tooltip.
4. `app/qml/BlockRegister.qml`: its entry, `<Name>Ui {}`.
5. Where its face or panel shows readings, the mock engine (`engine/core/mockengine.cpp`) can make some for the Qt Quick tests.

## Its documentation

- Its line in [the list of blocks](../blocks.md), and in the topic page it belongs to, such as [hearing](../hearing.md).
- Its section in the handbook, `doc/index.docbook`, with the id `block-<typeid>`, its dots made dashes (`block-effect-reverb`).

## The checks

The block's own tests, and the ones that hold every block to the same rules: `tools.block-folders`, `tools.help-pages`, `tools.handbook-blocks`, `qml.mainwindow` and the `i18n` tests, then `all_qmllint` and `clang-format`, as [Development](../development.md) describes. A block without an icon, an entry, a help page or a test does not configure, and the message says which is missing.

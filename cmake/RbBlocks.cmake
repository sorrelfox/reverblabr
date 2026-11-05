# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

#[=======================================================================[.rst:
RbBlocks
--------

One folder per block (ADR-0035): a block's CMakeLists.txt names its files by
role, and each part of the build takes its share.

::

  rb_add_block(<name>
               [REQUIRES <variable>...]
               [NO_HELP_PAGE]
               ENGINE <file>...
               UI <file>...
               ICONS <file>...
               [PRESETS <file>...]
               [TESTS <file>... [TEST_TARGET <target>]]
               [UI_TESTS <file>...]
               [QML_TESTS <file>...])

Files are named relative to the folder of the CMakeLists.txt that calls it,
one by one rather than globbed, so that adding one is a change somebody
reviews.

``ENGINE``
  The block's own C++, built into ``reverblabr-blocks``, where the layer rule
  holds: no Qt, no KDE Frameworks, no PipeWire.
``UI``
  The QML files and the C++ that only this block's interface uses, built
  into the interface's QML module. A QML file keeps its file name as its
  type name, wherever it is.
``ICONS``
  The block's icons, ``<typeId>.svg``, at ``icons/`` in the QML module.
``PRESETS``
  Factory presets, ``presets/<typeId>/<file>.json``, compiled into the
  engine.
``TESTS``
  GoogleTest sources, built into ``reverblabr-blocks-test``, or into
  ``TEST_TARGET`` where the tests need another one.
``UI_TESTS``
  GoogleTest sources for the block's interface C++, built into
  ``reverblabr-app-test``.
``QML_TESTS``
  ``tst_*.qml`` files, run with all the others by ``qml.mainwindow``.
``REQUIRES``
  The build options the block needs. Where one is off, the block's C++ and
  its GoogleTest sources are left out. Its interface, its presets and its
  QML tests are built all the same, because the interface names every block
  whether or not this build has it.

A block needs an icon, a help page (a ``*Help.qml`` among ``UI``) and a
test: without one, configuration stops and says which is missing.
``NO_HELP_PAGE`` is for the one kind of block that has none of its own, a
plugin's, whose help is its maker's.

The other parts of the build read what the blocks declared with
``rb_blocks_get(<kind> <variable>)``, where ``<kind>`` is ``UI``, ``ICONS``,
``PRESETS``, ``UI_INCLUDE_DIRS``, ``QML_TEST_DIRS``, ``TEST_TARGETS`` or
``TESTS_<target>``. A block's folder has to be added before them.
#]=======================================================================]

function(rb_add_block name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "NO_HELP_PAGE" "TEST_TARGET"
        "REQUIRES;ENGINE;UI;ICONS;PRESETS;TESTS;UI_TESTS;QML_TESTS")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "rb_add_block(${name}): unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    set(where "rb_add_block(${name}) in ${CMAKE_CURRENT_SOURCE_DIR}")

    # The checklist a new block is held to.
    if(NOT arg_ENGINE)
        message(FATAL_ERROR "${where}: a block needs its C++ (ENGINE).")
    endif()
    if(NOT arg_ICONS)
        message(FATAL_ERROR "${where}: a block needs an icon (ICONS <typeId>.svg).")
    endif()
    set(help "")
    foreach(file IN LISTS arg_UI)
        if(file MATCHES "Help\\.qml$")
            set(help "${file}")
        endif()
    endforeach()
    if(NOT help AND NOT arg_NO_HELP_PAGE)
        message(FATAL_ERROR "${where}: a block needs a help page (a *Help.qml among UI).")
    endif()
    if(NOT arg_TESTS AND NOT arg_UI_TESTS AND NOT arg_QML_TESTS)
        message(FATAL_ERROR "${where}: a block needs a test (TESTS, UI_TESTS or QML_TESTS).")
    endif()
    if(arg_TEST_TARGET AND NOT arg_TESTS)
        message(FATAL_ERROR "${where}: TEST_TARGET names where the TESTS go, and there are none.")
    endif()

    foreach(kind IN ITEMS ENGINE UI ICONS PRESETS TESTS UI_TESTS QML_TESTS)
        set(${kind} "")
        foreach(file IN LISTS arg_${kind})
            set(path "${CMAKE_CURRENT_SOURCE_DIR}/${file}")
            if(NOT EXISTS "${path}")
                message(FATAL_ERROR "${where}: there is no ${file}.")
            endif()
            list(APPEND ${kind} "${path}")
        endforeach()
    endforeach()
    foreach(path IN LISTS ICONS)
        if(NOT path MATCHES "\\.svg$")
            message(FATAL_ERROR "${where}: an icon is an SVG file: ${path}")
        endif()
    endforeach()
    foreach(path IN LISTS PRESETS)
        if(NOT path MATCHES "/presets/[^/]+/[^/]+\\.json$")
            message(FATAL_ERROR "${where}: a preset is presets/<typeId>/<file>.json: ${path}")
        endif()
    endforeach()
    foreach(path IN LISTS QML_TESTS)
        get_filename_component(file "${path}" NAME)
        if(NOT file MATCHES "^tst_.*\\.qml$")
            message(FATAL_ERROR "${where}: a QML test is a tst_*.qml file: ${path}")
        endif()
    endforeach()

    set(built TRUE)
    foreach(variable IN LISTS arg_REQUIRES)
        if(NOT ${variable})
            set(built FALSE)
        endif()
    endforeach()

    if(built)
        target_sources(reverblabr-blocks PRIVATE ${ENGINE})
        if(TESTS)
            set(target reverblabr-blocks-test)
            if(arg_TEST_TARGET)
                set(target ${arg_TEST_TARGET})
            endif()
            set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_TEST_TARGETS ${target})
            set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_TESTS_${target} ${TESTS})
        endif()
    endif()

    set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_UI ${UI})
    set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_ICONS ${ICONS})
    set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_PRESETS ${PRESETS})
    foreach(path IN LISTS UI)
        if(path MATCHES "\\.h$")
            get_filename_component(dir "${path}" DIRECTORY)
            set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_UI_INCLUDE_DIRS "${dir}")
        endif()
    endforeach()
    if(UI_TESTS)
        set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_TEST_TARGETS reverblabr-app-test)
        set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_TESTS_reverblabr-app-test ${UI_TESTS})
    endif()
    foreach(path IN LISTS QML_TESTS)
        get_filename_component(dir "${path}" DIRECTORY)
        set_property(GLOBAL APPEND PROPERTY RB_BLOCKS_QML_TEST_DIRS "${dir}")
    endforeach()
endfunction()

function(rb_blocks_get kind variable)
    get_property(value GLOBAL PROPERTY RB_BLOCKS_${kind})
    list(REMOVE_DUPLICATES value)
    set(${variable} "${value}" PARENT_SCOPE)
endfunction()

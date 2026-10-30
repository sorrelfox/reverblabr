# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The clean-room git hooks in tools/hooks, run against a throwaway repository.
find_program(RB_BASH bash REQUIRED)
find_program(RB_GIT git REQUIRED)
add_test(NAME tools.git-hooks COMMAND ${RB_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/test_git_hooks.sh)

# The application's identity: the desktop entry, the AppStream data and the
# name KAboutData carries all have to be the same string, or the portal
# cannot find the application and the window has no icon.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME tools.desktop-entry
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_desktop_entry.py)
set_tests_properties(tools.desktop-entry PROPERTIES PASS_REGULAR_EXPRESSION "PASS: desktop entry")
set_tests_properties(tools.git-hooks PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS: git hooks"
    TIMEOUT 60
)

# Nothing in the QML draws with a colour, font or corner that is not a
# token, so switching the appearance switches all of it.
add_test(NAME tools.design-tokens
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_design_tokens.py)
set_tests_properties(tools.design-tokens PROPERTIES PASS_REGULAR_EXPRESSION "PASS: design tokens")

# Every package tools/deps installs is on docs/dependencies.md, with what it
# is for, so a new dependency cannot land without the person installing it
# being told what it gives them.
add_test(NAME tools.dependencies-documented
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_dependencies_documented.py)
set_tests_properties(tools.dependencies-documented PROPERTIES PASS_REGULAR_EXPRESSION "PASS: dependencies documented")

# Words are written in the colours made to be read: a label's colour
# is never one of the five fills that fall under 4.5:1 on a light surface.
add_test(NAME tools.readable-colours
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_readable_colours.py)
set_tests_properties(tools.readable-colours PROPERTIES PASS_REGULAR_EXPRESSION "PASS: readable colours")

# The title bar's window buttons are the desktop's glyphs, not the
# prototype's, which has maximise and restore the other way round.
add_test(NAME tools.window-glyphs
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_window_glyphs.py)
set_tests_properties(tools.window-glyphs PROPERTIES PASS_REGULAR_EXPRESSION "PASS: window glyphs")

# The public D-Bus API as released (ADR-0020): nothing pinned in
# dbus/io.github.reverblabr.Engine1.api disappears or changes, nothing is
# added without being pinned, and nothing pinned goes undocumented in
# docs/dbus-api.md.
add_test(NAME dbus.public-surface
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_dbus_public_surface.py)
set_tests_properties(dbus.public-surface PROPERTIES PASS_REGULAR_EXPRESSION "PASS: public D-Bus surface")

# reverblabr-ctl's exit codes and ctl.* error codes, which scripts rely on,
# are the ones docs/ctl.md and the man page describe.
add_test(NAME tools.ctl-documented
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_ctl_documented.py)
set_tests_properties(tools.ctl-documented PROPERTIES PASS_REGULAR_EXPRESSION "PASS: ctl documented")

# **The handbook follows the library**: every block has a section under
# the name the library gives it, and none describes a block that is gone.
add_test(NAME tools.handbook-blocks
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_handbook.py blocks)
set_tests_properties(tools.handbook-blocks PROPERTIES PASS_REGULAR_EXPRESSION "PASS: handbook blocks")
# **Every block has help, or is listed as still to get it**, and every help
# page is written one marked message a line, so translators get it in
# paragraphs (ADR-0029). That a page names every control of its panel is
# tst_blockhelp.qml's to check.
add_test(NAME tools.help-pages
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_help_pages.py)
set_tests_properties(tools.help-pages PROPERTIES PASS_REGULAR_EXPRESSION "PASS: help pages")
# The release the handbook says it describes is VERSION's, as the AppStream
# data's newest is (tools.desktop-entry).
add_test(NAME tools.handbook-release
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_handbook.py release)
set_tests_properties(tools.handbook-release PROPERTIES PASS_REGULAR_EXPRESSION "PASS: handbook release")
# **The handbook is valid DocBook**, checked even in a build that does not
# build it, as long as KDocTools' checker is there; skipped where it is not.
find_program(RB_CHECKXML6 checkXML6)
add_test(NAME tools.handbook-valid
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_handbook.py valid "$<$<BOOL:${RB_CHECKXML6}>:${RB_CHECKXML6}>")
set_tests_properties(tools.handbook-valid PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS: handbook valid"
    SKIP_RETURN_CODE 77
    TIMEOUT 60
)

# **The version is VERSION's**: the release tools in a throwaway
# repository, and every program's --version against the file.
add_test(NAME tools.release COMMAND ${RB_BASH} ${CMAKE_CURRENT_SOURCE_DIR}/test_release.sh)
set_tests_properties(tools.release PROPERTIES PASS_REGULAR_EXPRESSION "PASS: release tools" TIMEOUT 60)
add_test(NAME tools.versions
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_versions.py
                 $<TARGET_FILE:reverblabr> $<TARGET_FILE:reverblabrd> $<TARGET_FILE:reverblabr-ctl>
                 $<TARGET_FILE:reverblabr-plugin-host>)
set_tests_properties(tools.versions PROPERTIES PASS_REGULAR_EXPRESSION "PASS: versions" TIMEOUT 120)

# The performance report says a figure got worse when it did, and only
# then: on made-up results, and on the DSP benchmark with a block made
# slower on purpose, which is what the nightly run's alert rests on.
add_test(NAME tools.perf-report
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_perf_report.py
                 $<TARGET_FILE:reverblabr-bench-dsp>)
set_tests_properties(tools.perf-report PROPERTIES PASS_REGULAR_EXPRESSION "PASS: perf report" TIMEOUT 120)

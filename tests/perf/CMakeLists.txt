# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Benchmarks that write their figures down (perfreport.h) for
# tools/perf/report.py to hold against a baseline. The soak and the
# session's cost run in a private PipeWire, so they live in tests/pw
# (pw.soak, pw.session-cost); these two need none.
#
# **Not part of the default run**, like the design comparison: a figure
# from a debug build, or from a machine busy building something else, says
# nothing worth keeping. The test presets leave the `perf` label out, and
# `ctest --preset perf` runs these, short, in the release build. The
# figures go to perf-results/ in the build tree.
set(RB_PERF_RESULTS "${CMAKE_BINARY_DIR}/perf-results")

# What every registered block costs.
add_executable(reverblabr-bench-dsp bench_dsp.cpp perfreport.h)
target_link_libraries(reverblabr-bench-dsp PRIVATE reverblabr-blocks reverblabr-dsp-testkit)
set_target_properties(reverblabr-bench-dsp PROPERTIES AUTOMOC OFF AUTORCC OFF)
add_test(NAME perf.dsp COMMAND reverblabr-bench-dsp --seconds 3 --json ${RB_PERF_RESULTS}/dsp.json)

# What a frame of the canvas costs, with fifty tiles on it.
add_executable(reverblabr-bench-canvas bench_canvas.cpp perfreport.h)
target_compile_definitions(reverblabr-bench-canvas PRIVATE BENCH_CANVAS_QML="${CMAKE_CURRENT_SOURCE_DIR}/BenchCanvas.qml")
target_link_libraries(reverblabr-bench-canvas PRIVATE
    reverblabr-ui
    reverblabr-uiplugin
    reverblabr-styleplugin
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    KF6::I18n
    KF6::I18nQml
)
add_test(NAME perf.canvas COMMAND reverblabr-bench-canvas --frames 180 --json ${RB_PERF_RESULTS}/canvas.json)
# A home of its own, as the QML tests have, so nothing the window writes
# reaches the home directory of whoever runs it.
set(RB_PERF_HOME "${CMAKE_CURRENT_BINARY_DIR}/test-home")
file(MAKE_DIRECTORY "${RB_PERF_HOME}")
set_tests_properties(perf.canvas PROPERTIES
    ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;LC_ALL=en_US.UTF-8;XDG_DATA_HOME=${RB_PERF_HOME}/data;XDG_CONFIG_HOME=${RB_PERF_HOME}/config;XDG_STATE_HOME=${RB_PERF_HOME}/state;XDG_CACHE_HOME=${RB_PERF_HOME}/cache"
)
set_tests_properties(perf.dsp perf.canvas PROPERTIES LABELS perf TIMEOUT 600)

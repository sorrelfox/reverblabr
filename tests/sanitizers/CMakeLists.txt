# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Proves that the real-time sanitizer catches what it should: an allocation in
# a function marked RB_NONBLOCKING must be reported. Only the rtsan preset
# builds with the sanitizer, so the check exists only there.
if("realtime" IN_LIST ECM_ENABLE_SANITIZERS)
    add_executable(reverblabr-rtsan-violation rtsan_violation.cpp)
    target_link_libraries(reverblabr-rtsan-violation PRIVATE reverblabr-dsp)
    # The violation is deliberate, so Clang's compile-time check must not reject it.
    target_compile_options(reverblabr-rtsan-violation PRIVATE -Wno-function-effects)
    set_target_properties(reverblabr-rtsan-violation PROPERTIES AUTOMOC OFF AUTORCC OFF)

    add_test(NAME sanitizers.rtsan-reports-allocation COMMAND reverblabr-rtsan-violation)
    set_tests_properties(sanitizers.rtsan-reports-allocation PROPERTIES
        PASS_REGULAR_EXPRESSION "real-time unsafe function .malloc. in real-time context"
    )
endif()

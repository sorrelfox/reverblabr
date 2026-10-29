# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Catalog loading: compile the made-up "xx" catalog into a private locale
# directory and check that C++ and QML strings come out translated.
set(test_locale_dir ${CMAKE_CURRENT_BINARY_DIR}/locale)
set(test_catalog ${test_locale_dir}/xx/LC_MESSAGES/reverblabr.mo)
add_custom_command(
    OUTPUT ${test_catalog}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${test_locale_dir}/xx/LC_MESSAGES
    COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check -o ${test_catalog} ${CMAKE_CURRENT_SOURCE_DIR}/xx.po
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/xx.po
    COMMENT "Compiling the xx test catalog"
    VERBATIM
)
add_custom_target(reverblabr-test-catalog DEPENDS ${test_catalog})

add_executable(reverblabr-catalog-test test_catalog.cpp)
add_dependencies(reverblabr-catalog-test reverblabr-test-catalog)
target_compile_definitions(reverblabr-catalog-test PRIVATE TEST_LOCALE_DIR="${test_locale_dir}")
target_link_libraries(reverblabr-catalog-test PRIVATE
    reverblabr-ui
    reverblabr-uiplugin
    Qt6::Qml
    Qt6::Quick # makes QTEST_MAIN create a QGuiApplication, which the window needs
    Qt6::Test
    KF6::I18n
    KF6::I18nQml
)
# gettext needs a real locale to load catalogs at all; the test skips itself
# (with a reason) when en_US.UTF-8 isn't installed.
# Its XDG directories are its own: the QML engine keeps a disk cache, which
# otherwise went to the real ~/.cache.
set(catalog_test_home ${CMAKE_CURRENT_BINARY_DIR}/test-home)
add_test(NAME i18n.catalog COMMAND reverblabr-catalog-test)
set_tests_properties(i18n.catalog PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_CONTROLS_STYLE=Basic;LANG=en_US.UTF-8;XDG_DATA_HOME=${catalog_test_home}/data;XDG_CONFIG_HOME=${catalog_test_home}/config;XDG_STATE_HOME=${catalog_test_home}/state;XDG_CACHE_HOME=${catalog_test_home}/cache"
    SKIP_REGULAR_EXPRESSION "SKIP   : "
)

# String extraction, and the literal-string lint (on its fixtures, and on the
# real GUI sources, which must stay clean).
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME i18n.extract COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_extract.py)
add_test(NAME i18n.lint-selftest COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_lint_i18n.py)
add_test(NAME i18n.lint-app COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/lint-i18n.py ${PROJECT_SOURCE_DIR}/app)
add_test(NAME i18n.lint-plasmoid COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/lint-i18n.py ${PROJECT_SOURCE_DIR}/plasmoid)
set_tests_properties(i18n.extract PROPERTIES PASS_REGULAR_EXPRESSION "PASS: extraction")
set_tests_properties(i18n.lint-selftest PROPERTIES PASS_REGULAR_EXPRESSION "PASS: lint-i18n fixtures")
# The desktop entry, the notification events and the AppStream data:
# extracted into reverblabr-data.pot, and a catalog merged back through the
# same script the build uses.
add_test(NAME i18n.data-translations
         COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/test_data_translations.py ${CMAKE_COMMAND})
set_tests_properties(i18n.data-translations PROPERTIES PASS_REGULAR_EXPRESSION "PASS: data translations")

# The key screens in the pseudo-locale: every string the catalog has
# comes out as ⟦áççéñtéð ẋǿẋǿ⟧, 40 % longer, and pseudocheck reports what
# has no brackets (never went through the catalog) or no longer fits. The
# catalog is made from the sources on every run by pseudo_check.py, so it is
# never stale, and never installed. Offscreen, on the mock engine, with a
# home of its own; screenshots and report.json land in the test's folder.
add_executable(reverblabr-pseudo-check pseudocheck.cpp)
target_compile_definitions(reverblabr-pseudo-check PRIVATE PSEUDO_CHECK_QML="${CMAKE_CURRENT_SOURCE_DIR}/PseudoCheck.qml")
target_link_libraries(reverblabr-pseudo-check PRIVATE
    reverblabr-ui
    reverblabr-uiplugin
    reverblabr-styleplugin
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    KF6::I18n
    KF6::I18nQml
)
set(pseudo_home ${CMAKE_CURRENT_BINARY_DIR}/pseudo-home)
# **English underneath, whatever the session speaks**: the catalog is the only
# translation. LANGUAGE and LC_ALL beat LANG, and in a German session Qt would
# put its own German under the check ("Strg+N"), which reads as untranslated.
set(pseudo_env
    "QT_QPA_PLATFORM=offscreen;LANG=en_US.UTF-8;LC_ALL=en_US.UTF-8;LANGUAGE=en_US;XDG_DATA_HOME=${pseudo_home}/data;XDG_CONFIG_HOME=${pseudo_home}/config;XDG_STATE_HOME=${pseudo_home}/state;XDG_CACHE_HOME=${pseudo_home}/cache")
# In the application's own style, and in the desktop's, which it follows by
# default: they are not the same size, and a word can fit one and not the
# other.
foreach(appearance reverblabr-dark system-dark)
    add_test(NAME i18n.pseudo-locale.${appearance}
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/pseudo_check.py
                     --checker $<TARGET_FILE:reverblabr-pseudo-check>
                     --out ${CMAKE_CURRENT_BINARY_DIR}/pseudo-${appearance}
                     --appearance ${appearance})
endforeach()
set_tests_properties(i18n.pseudo-locale.reverblabr-dark PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS: pseudo-locale," TIMEOUT 600 ENVIRONMENT "${pseudo_env}")
set_tests_properties(i18n.pseudo-locale.system-dark PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS: pseudo-locale," TIMEOUT 600 ENVIRONMENT "${pseudo_env};QT_QPA_PLATFORMTHEME=kde")
# The check proving it can fail: a hard-coded string, and a translated one
# given too little room, put on the first screen on purpose.
foreach(fault hardcoded truncated)
    if(fault STREQUAL "hardcoded")
        set(kind untranslated)
    else()
        set(kind "cut off")
    endif()
    add_test(NAME i18n.pseudo-locale.selftest-${fault}
             COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/pseudo_check.py
                     --checker $<TARGET_FILE:reverblabr-pseudo-check>
                     --out ${CMAKE_CURRENT_BINARY_DIR}/pseudo-selftest-${fault}
                     --appearance reverblabr-dark --inject ${fault} --expect ${kind})
    set_tests_properties(i18n.pseudo-locale.selftest-${fault} PROPERTIES
        PASS_REGULAR_EXPRESSION "PASS: pseudo-locale self-test" TIMEOUT 600 ENVIRONMENT "${pseudo_env}")
endforeach()

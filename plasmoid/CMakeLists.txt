# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# **The panel widget** (ADR-0024): every saved session with Run and Stop
# and how loud it is, in a Plasma panel, whether the window is open or not.
#
# Two parts. The package (package/), which Plasma loads by its id: the
# metadata and the QML, with the words through KI18n in the widget's own
# translation domain. And a small QML module, io.github.reverblabr.plasmoid,
# which the package imports: the sessions, from the engine's public D-Bus
# API, and their levels, from the memory the engine publishes them in.
# Neither needs anything of Plasma to build; Plasma loads them.

set(RB_PLASMOID_ID "io.github.reverblabr.sessions")
set(RB_PLASMOID_ID "${RB_PLASMOID_ID}" PARENT_SCOPE)

ecm_add_qml_module(reverblabr-plasmoidplugin
    URI io.github.reverblabr.plasmoid
    GENERATE_PLUGIN_SOURCE
    DEPENDENCIES
        QtQml
)
target_sources(reverblabr-plasmoidplugin PRIVATE
    sessionlist.cpp
    sessionlist.h
)
target_link_libraries(reverblabr-plasmoidplugin PRIVATE reverblabr-telemetry Qt6::Core Qt6::DBus Qt6::Qml)
ecm_finalize_qml_module(reverblabr-plasmoidplugin DESTINATION ${KDE_INSTALL_QMLDIR})

# **The package as Plasma finds it**, in the build tree as well as
# installed: the metadata with the version filled in, beside the QML. The
# tests and kpackagetool6 read this copy.
set(RB_PLASMOID_PACKAGE "${CMAKE_CURRENT_BINARY_DIR}/package")
set(RB_PLASMOID_PACKAGE "${RB_PLASMOID_PACKAGE}" PARENT_SCOPE)
configure_file(package/metadata.json "${RB_PLASMOID_PACKAGE}/metadata.json" @ONLY)
set(RB_PLASMOID_QML
    CompactRepresentation.qml
    FullRepresentation.qml
    LevelBar.qml
    SessionRow.qml
    main.qml
    status.js
)
foreach(_rb_qml IN LISTS RB_PLASMOID_QML)
    configure_file(package/contents/ui/${_rb_qml} "${RB_PLASMOID_PACKAGE}/contents/ui/${_rb_qml}" COPYONLY)
endforeach()

install(DIRECTORY "${RB_PLASMOID_PACKAGE}/"
        DESTINATION ${KDE_INSTALL_DATADIR}/plasma/plasmoids/${RB_PLASMOID_ID})

# **qmllint for the package**, as part of all_qmllint. The package is not a
# QML module, so Qt does not lint it by itself. Its words are Plasma's
# global i18n calls, which the shell puts on each widget's context in the
# widget's translation domain and which qmllint cannot know about: an
# unqualified name is reported as information here rather than as a
# warning, and the ids are bound (ComponentBehavior: Bound) so that every
# other name is qualified.
set(_rb_plasmoid_lint_files)
foreach(_rb_qml IN LISTS RB_PLASMOID_QML)
    if(_rb_qml MATCHES "\\.qml$")
        list(APPEND _rb_plasmoid_lint_files "package/contents/ui/${_rb_qml}")
    endif()
endforeach()
add_custom_target(reverblabr-plasmoid_qmllint
    COMMAND Qt6::qmllint -I "${QT_QML_OUTPUT_DIRECTORY}" --unqualified info ${_rb_plasmoid_lint_files}
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Linting the panel widget's QML"
    VERBATIM
)
add_dependencies(reverblabr-plasmoid_qmllint reverblabr-plasmoidplugin)
if(TARGET all_qmllint)
    add_dependencies(all_qmllint reverblabr-plasmoid_qmllint)
endif()

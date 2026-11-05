# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# **A session's telemetry in shared memory** (ADR-0018), on its own: the
# panel widget reads the levels from it too, inside the desktop shell's
# process, and must not take the engine in with it. Position-independent,
# because the widget's QML plugin is a shared library.
add_library(reverblabr-telemetry STATIC
    sharedtelemetry.cpp
    sharedtelemetry.h
)
target_include_directories(reverblabr-telemetry PUBLIC ${PROJECT_SOURCE_DIR}/engine)
# telemetry.h, which the region is laid out from, declares QObjects; the
# real-time headers it includes need nothing built.
target_link_libraries(reverblabr-telemetry PUBLIC Qt6::Core)
set_target_properties(reverblabr-telemetry PROPERTIES AUTOMOC OFF AUTORCC OFF POSITION_INDEPENDENT_CODE ON)

# Session model and engine facade: the only engine API the GUI uses. QtCore
# (and later QtDBus) only, no KDE Frameworks, so it can move into a daemon.
add_library(reverblabr-engine STATIC
    audiogramfit.cpp
    audiogramfit.h
    audiosystem.cpp
    audiosystem.h
    compressormodes.cpp
    compressormodes.h
    lastlimit.cpp
    lastlimit.h
    diagnostics.cpp
    diagnostics.h
    dynamicscurve.cpp
    dynamicscurve.h
    dynamicsmigration.cpp
    dynamicsmigration.h
    equalizercurve.cpp
    equalizercurve.h
    editcommand.cpp
    editcommand.h
    engine.cpp
    engine.h
    edithistory.cpp
    edithistory.h
    enginefacade.h
    engineinfo.cpp
    engineinfo.h
    errorcodes.h
    logring.cpp
    logring.h
    noticecodes.h
    mockengine.cpp
    mockengine.h
    blockcatalogue.cpp
    blockcatalogue.h
    broadcasts.cpp
    broadcasts.h
    paramjson.cpp
    paramjson.h
    pluginscanner.cpp
    pluginscanner.h
    preset.cpp
    preset.h
    presetactions.cpp
    presetactions.h
    presetstore.cpp
    presetstore.h
    quitapplications.cpp
    quitapplications.h
    recordinglibrary.cpp
    recordinglibrary.h
    recordingnames.cpp
    recordingnames.h
    recordingplayer.cpp
    recordingplayer.h
    recordings.cpp
    recordings.h
    scopeclaims.cpp
    scopeclaims.h
    secretstore.cpp
    secretstore.h
    sessionlisting.cpp
    sessionlisting.h
    sessionplan.cpp
    sessionplan.h
    sessionruntime.cpp
    sessionruntime.h
    session.cpp
    session.h
    sessionstore.cpp
    sessionstore.h
    telemetry.cpp
    telemetry.h
    timerrunner.cpp
    timerrunner.h
    timers.cpp
    timers.h
    virtualdevices.cpp
    virtualdevices.h
    xrunhistory.h
)
# The blocks are included as "blocks/<folder>/<file>.h", from the top (ADR-0035).
target_include_directories(reverblabr-engine PUBLIC ${PROJECT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-engine
    PUBLIC Qt6::Core reverblabr-telemetry
    # The recordings index is SQLite, through Qt: a Qt module with no
    # GUI in it, as the daemon the engine becomes allows.
    PRIVATE Qt6::Sql
    PRIVATE reverblabr-pw reverblabr-blocks reverblabr-record reverblabr-stream reverblabr-pluginproto reverblabr-version
    # A Broadcast block's title, from the media player it follows.
    PRIVATE reverblabr-mpris
)
# A Broadcast block's password, in the desktop's secret store (ADR-0026).
if(RB_BUILD_BROADCAST)
    target_link_libraries(reverblabr-engine PRIVATE Qt6Keychain::Qt6Keychain)
endif()
# A Live Stream block's stream, where the build has the block (ADR-0027).
if(RB_BUILD_LIVESTREAM)
    target_sources(reverblabr-engine PRIVATE livestreams.cpp livestreams.h)
    target_link_libraries(reverblabr-engine PRIVATE reverblabr-livestream)
endif()
# The facade is a QObject, so moc runs here.
set_target_properties(reverblabr-engine PROPERTIES AUTOMOC ON AUTORCC ON)

# **The factory presets travel inside the binary**. They are read-only
# because there is no file for anybody to edit, rather than because of a
# permission somebody could change — and because they are compiled in, there
# is no installation step that can leave them behind. Each block's folder
# lists its own (blocks/<folder>/presets/<typeId>/), one by one rather than
# globbed, so that adding one is a change somebody reviews; here they are
# kept under the type id, as before.
rb_blocks_get(PRESETS rb_factory_presets)
foreach(preset IN LISTS rb_factory_presets)
    get_filename_component(preset_name "${preset}" NAME)
    get_filename_component(preset_dir "${preset}" DIRECTORY)
    get_filename_component(preset_type "${preset_dir}" NAME)
    set_source_files_properties("${preset}" PROPERTIES QT_RESOURCE_ALIAS "${preset_type}/${preset_name}")
endforeach()
qt_add_resources(reverblabr-engine "reverblabr-factory-presets"
    PREFIX "/io/github/reverblabr/presets"
    FILES ${rb_factory_presets}
)

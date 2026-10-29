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
    diagnostics.cpp
    diagnostics.h
    dynamicscurve.cpp
    dynamicscurve.h
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
    recordinglibrary.cpp
    recordinglibrary.h
    recordingnames.cpp
    recordingnames.h
    recordingplayer.cpp
    recordingplayer.h
    recordings.cpp
    recordings.h
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
    virtualdevices.cpp
    virtualdevices.h
    xrunhistory.h
)
target_include_directories(reverblabr-engine PUBLIC ${PROJECT_SOURCE_DIR}/engine)
target_link_libraries(reverblabr-engine
    PUBLIC Qt6::Core reverblabr-telemetry
    # The recordings index is SQLite, through Qt: a Qt module with no
    # GUI in it, as the daemon the engine becomes allows.
    PRIVATE Qt6::Sql
    PRIVATE reverblabr-pw reverblabr-blocks reverblabr-record reverblabr-pluginproto reverblabr-version
)
# The facade is a QObject, so moc runs here.
set_target_properties(reverblabr-engine PROPERTIES AUTOMOC ON AUTORCC ON)

# **The factory presets travel inside the binary**. They are read-only
# because there is no file for anybody to edit, rather than because of a
# permission somebody could change — and because they are compiled in, there
# is no installation step that can leave them behind. Listed one by one
# rather than globbed, so that adding one is a change somebody reviews.
qt_add_resources(reverblabr-engine "reverblabr-factory-presets"
    PREFIX "/io/github/reverblabr/presets"
    BASE "../presets"
    FILES
        ../presets/advanced.dehum/10-mains50Hz.json
        ../presets/advanced.dehum/20-mains60Hz.json
        ../presets/effect.dynamics/10-default.json
        ../presets/effect.dynamics/20-gentleLeveling.json
        ../presets/effect.dynamics/30-voice.json
        ../presets/effect.dynamics/40-noiseGate.json
        ../presets/effect.dynamics/50-hardLimiting.json
        ../presets/effect.equalizer10/010-flat.json
        ../presets/effect.equalizer10/020-bassBoost.json
        ../presets/effect.equalizer10/030-lessBass.json
        ../presets/effect.equalizer10/040-trebleBoost.json
        ../presets/effect.equalizer10/050-lessTreble.json
        ../presets/effect.equalizer10/060-vocalPresence.json
        ../presets/effect.equalizer10/070-podcastVoice.json
        ../presets/effect.equalizer10/080-loudness.json
        ../presets/effect.equalizer10/090-headphones.json
        ../presets/effect.equalizer10/100-laptopSpeakers.json
        ../presets/effect.equalizer10/110-warm.json
        ../presets/effect.equalizer10/120-bright.json
        ../presets/effect.equalizer10/130-telephone.json
        ../presets/effect.equalizer10/140-oldRadio.json
        ../presets/effect.equalizer10/150-rumbleCut.json
        ../presets/effect.equalizer10/160-hissCut.json
        ../presets/effect.equalizer10/170-lessMud.json
        ../presets/effect.equalizer10/180-lessHarshness.json
        ../presets/effect.equalizer10/190-air.json
        ../presets/effect.equalizer10/200-smile.json
        ../presets/effect.reverb/10-smallRoom.json
        ../presets/effect.reverb/20-hall.json
        ../presets/effect.reverb/30-plate.json
        ../presets/effect.volume/10-halfVolume.json
        ../presets/effect.volume/20-unityVolume.json
        ../presets/effect.volume/30-doubleVolume.json
        ../presets/hearing.compensation16/010-flat.json
        ../presets/hearing.compensation16/020-gentleSlope.json
        ../presets/hearing.compensation16/030-steeperSlope.json
        ../presets/hearing.compensation64/010-flat.json
        ../presets/hearing.compensation64/020-gentleSlope.json
        ../presets/hearing.compensation64/030-steeperSlope.json
)

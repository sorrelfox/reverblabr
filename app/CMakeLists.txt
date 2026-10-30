# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# The GUI: the only part of Reverblabr that uses KDE Frameworks.

# Every i18n call in the GUI uses the application's catalog explicitly.
add_compile_definitions(TRANSLATION_DOMAIN="reverblabr")

# The QML user interface is a static QML module, so the application and the
# QML tests load exactly the same components.
add_library(reverblabr-ui STATIC)
ecm_add_qml_module(reverblabr-ui
    URI io.github.reverblabr
    GENERATE_PLUGIN_SOURCE
    DEPENDENCIES
        QtQuick
        QtQuick.Controls
        QtQuick.Shapes
        org.kde.kirigami
        org.kde.ki18n
        org.kde.coreaddons
)
# A QML singleton has to say so in the module's qmldir, or every use of it
# is a type error that only qmllint sees.
set_source_files_properties(qml/AppColors.qml qml/AppFonts.qml qml/AppMetrics.qml qml/BlockFaces.qml
    qml/BlockPanels.qml qml/ParamFormat.qml qml/PluginWords.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The blocks' help pages, and the window they are shown in.
set_source_files_properties(qml/BlockHelp.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The prototype's own glyphs, by name.
set_source_files_properties(qml/AppGlyphs.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The one voice the window speaks to a screen reader with, unasked.
set_source_files_properties(qml/Announcer.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
ecm_target_qml_sources(reverblabr-ui SOURCES
    qml/AppGlyphs.qml
)
ecm_target_qml_sources(reverblabr-ui SOURCES
    qml/AppColors.qml
    qml/AppFonts.qml
    qml/AppMetrics.qml
    qml/AppDialog.qml
    qml/AppPromptDialog.qml
    qml/Announcer.qml
    qml/ApplicationPicker.qml
    qml/ApplicationSourceFace.qml
    qml/ApplicationSourceHelp.qml
    qml/ApplicationSourcePanel.qml
    qml/AutoLevelFace.qml
    qml/AutoLevelHelp.qml
    qml/AutoLevelPanel.qml
    qml/AutoLevelWords.qml
    qml/BalanceHelp.qml
    qml/BassTrebleFace.qml
    qml/BassTrebleHelp.qml
    qml/BassTreblePanel.qml
    qml/BlockFaces.qml
    qml/BlockHelp.qml
    qml/BlockHelpWindow.qml
    qml/BlockLibrary.qml
    qml/BlockLibraryRow.qml
    qml/BlockPanels.qml
    qml/BlockPort.qml
    qml/BlockTile.qml
    qml/BroadcastHelp.qml
    qml/BroadcastPanel.qml
    qml/BroadcastWords.qml
    qml/LiveStreamHelp.qml
    qml/LiveStreamPanel.qml
    qml/LiveStreamWords.qml
    qml/NowPlayingRows.qml
    qml/CanvasPlaceholder.qml
    qml/CanvasWire.qml
    qml/ChannelsFace.qml
    qml/ChannelsHelp.qml
    qml/ChannelsPanel.qml
    qml/ChannelsWords.qml
    qml/CollapsibleSection.qml
    qml/CompressorWords.qml
    qml/CutoffFace.qml
    qml/CutoffPanel.qml
    qml/DeclickFace.qml
    qml/DeclickHelp.qml
    qml/DeclickPanel.qml
    qml/DeclickWords.qml
    qml/DehumFace.qml
    qml/DehumHelp.qml
    qml/DehumPanel.qml
    qml/DehumWords.qml
    qml/DenoiseFace.qml
    qml/DenoiseHelp.qml
    qml/DenoisePanel.qml
    qml/DenoiseState.qml
    qml/DestructiveMenuItem.qml
    qml/DeviceChannelRows.qml
    qml/DeviceFace.qml
    qml/DevicePicker.qml
    qml/DeviceState.qml
    qml/DiagnosticsSection.qml
    qml/DuckingFace.qml
    qml/DuckingHelp.qml
    qml/DuckingPanel.qml
    qml/DuckingWords.qml
    qml/DynamicsFace.qml
    qml/DynamicsHelp.qml
    qml/DynamicsPanel.qml
    qml/EqualizerBands.qml
    qml/EqualizerFace.qml
    qml/EqualizerHelp.qml
    qml/EqualizerPanel.qml
    qml/EchoFace.qml
    qml/EchoHelp.qml
    qml/EchoPanel.qml
    qml/EchoWords.qml
    qml/ElidedLabel.qml
    qml/ErrorBanner.qml
    qml/FilesPane.qml
    qml/FilterWords.qml
    qml/FirFilterFace.qml
    qml/FirFilterHelp.qml
    qml/FirFilterPanel.qml
    qml/FirFilterState.qml
    qml/FrequencyLoweringFace.qml
    qml/FrequencyLoweringHelp.qml
    qml/FrequencyLoweringPanel.qml
    qml/FrequencyMoveFace.qml
    qml/FrequencyMovePanel.qml
    qml/FrequencyMoves.qml
    qml/FrequencyMoveWords.qml
    qml/FrequencyRaisingFace.qml
    qml/FrequencyRaisingHelp.qml
    qml/FrequencyRaisingPanel.qml
    qml/GenericFace.qml
    qml/GenericParamPanel.qml
    qml/HearingBands.qml
    qml/HelpPage.qml
    qml/HelpSection.qml
    qml/HearingCompensation16Help.qml
    qml/HearingCompensation64Help.qml
    qml/HearingCompensationFace.qml
    qml/HearingCompensationPanel.qml
    qml/HighPassHelp.qml
    qml/InfoLine.qml
    qml/InputDeviceHelp.qml
    qml/InputDevicePanel.qml
    qml/InputSwitchControl.qml
    qml/InputSwitchFace.qml
    qml/InputSwitchHelp.qml
    qml/InputSwitchPanel.qml
    qml/JoinLrHelp.qml
    qml/LevelBar.qml
    qml/LevelMeter.qml
    qml/LimiterFace.qml
    qml/LimiterHelp.qml
    qml/LimiterPanel.qml
    qml/LimiterWords.qml
    qml/LowPassHelp.qml
    qml/MixerFace.qml
    qml/MixerHelp.qml
    qml/MixerPanel.qml
    qml/MixerStrip.qml
    qml/Main.qml
    qml/MonoEnhancerFace.qml
    qml/MonoEnhancerHelp.qml
    qml/MonoEnhancerPanel.qml
    qml/MultibandCompressorFace.qml
    qml/MultibandCompressorHelp.qml
    qml/MultibandCompressorPanel.qml
    qml/MultibandWords.qml
    qml/OutputDeviceHelp.qml
    qml/OutputDevicePanel.qml
    qml/PanHelp.qml
    qml/ParamFormat.qml
    qml/ParamRow.qml
    qml/ParametricEqBands.qml
    qml/ParametricEqFace.qml
    qml/ParametricEqHelp.qml
    qml/ParametricEqPanel.qml
    qml/PeakRmsFace.qml
    qml/PeakRmsHelp.qml
    qml/PeakRmsPanel.qml
    qml/PitchShiftFace.qml
    qml/PitchShiftHelp.qml
    qml/PitchShiftPanel.qml
    qml/PitchShiftWords.qml
    qml/PlaceholderMessage.qml
    qml/PluginFace.qml
    qml/PluginPanel.qml
    qml/PluginWords.qml
    qml/PresetsFooter.qml
    qml/RecordingInspectorWindow.qml
    qml/RecorderFace.qml
    qml/RecorderFileName.qml
    qml/RecorderHelp.qml
    qml/RecorderPanel.qml
    qml/RecorderWords.qml
    qml/RecordingCard.qml
    qml/ReverbFace.qml
    qml/ReverbHelp.qml
    qml/ReverbPanel.qml
    qml/ReverbWords.qml
    qml/SegmentedChoice.qml
    qml/SegmentedFrame.qml
    qml/SessionBound.qml
    qml/SessionCanvas.qml
    qml/SchedulePane.qml
    qml/SessionInfoPane.qml
    qml/SessionList.qml
    qml/SessionTabs.qml
    qml/SettingsPanel.qml
    qml/SidebarPane.qml
    qml/SimpleCompressorFace.qml
    qml/SimpleCompressorHelp.qml
    qml/SimpleCompressorPanel.qml
    qml/SpeechDenoiseFace.qml
    qml/SpeechDenoiseHelp.qml
    qml/SpeechDenoisePanel.qml
    qml/SpeechDenoiseState.qml
    qml/StatusBarRow.qml
    qml/StereoPositionFace.qml
    qml/StereoPositionPanel.qml
    qml/StereoPositionWords.qml
    qml/SyncFace.qml
    qml/SyncHelp.qml
    qml/SyncPanel.qml
    qml/SyncScale.qml
    qml/SystemAudioFace.qml
    qml/SystemAudioHelp.qml
    qml/SystemAudioPanel.qml
    qml/TimeShiftFace.qml
    qml/TimeShiftHelp.qml
    qml/TimeShiftPanel.qml
    qml/ShortcutTargets.qml
    qml/TimeShiftState.qml
    qml/TimerDialog.qml
    qml/ToolBarRow.qml
    qml/VolumeFace.qml
    qml/VolumeHelp.qml
    qml/VuFace.qml
    qml/VuGauge.qml
    qml/VuHelp.qml
    qml/VuPanel.qml
    qml/WaveFace.qml
    qml/VolumePanel.qml
    qml/WaveHelp.qml
    qml/WavePanel.qml
    qml/WaveWords.qml
    # The slider as the prototype draws it: one groove and one ring,
    # which the control style's Slider and a tile's face both draw with.
    qml/FaceSlider.qml
    qml/SliderGroove.qml
    qml/SliderHandle.qml
    # The frameless window's own title bar: its buttons and the grips
    # along its edges, and the title bar and frame any window of ours can
    # wear.
    qml/ResizeGrips.qml
    qml/WindowButton.qml
    qml/WindowChrome.qml
    qml/WindowTitleBar.qml
    # The virtual devices' own window, from the title bar's button,
    # and its lists.
    qml/VirtualDevicesWindow.qml
    qml/VirtualDeviceRow.qml
    qml/VirtualDeviceSection.qml
    # Every saved session in a table of its own.
    qml/SessionsWindow.qml
    # The system tray's popover.
    qml/TrayPopover.qml
)

# **The block icons ride with the QML module.** They are ours, drawn for
# this application and licensed CC-BY-SA-4.0 (app/icons), and each one is a
# single-colour mask so the theme and the block's category tint colour it.
# In the module's own resource root, a build that has the interface has the
# icons, with no install step and nothing to find at run time.
qt_target_qml_sources(reverblabr-ui RESOURCES
    icons/advanced.declick.svg
    icons/advanced.dehum.svg
    icons/advanced.denoise.svg
    icons/advanced.ducking.svg
    icons/advanced.firfilter.svg
    icons/advanced.inputswitch.svg
    icons/advanced.mixer.svg
    icons/advanced.parametriceq.svg
    icons/advanced.speechdenoise.svg
    icons/advanced.sync.svg
    icons/advanced.timeshift.svg
    icons/advanced.transcribe.svg
    icons/effect.autolevel.svg
    icons/effect.balance.svg
    icons/effect.basstreble.svg
    icons/effect.channels.svg
    icons/effect.dynamics.svg
    icons/effect.echo.svg
    icons/effect.equalizer10.svg
    icons/effect.highpass.svg
    icons/effect.limiter.svg
    icons/effect.lowpass.svg
    icons/effect.monoenhancer.svg
    icons/effect.multibandcompressor.svg
    icons/effect.pan.svg
    icons/effect.pitchshift.svg
    icons/effect.reverb.svg
    icons/effect.simplecompressor.svg
    icons/effect.volume.svg
    icons/meter.peakrms.svg
    icons/meter.vu.svg
    icons/output.broadcast.svg
    icons/output.device.svg
    icons/output.livestream.svg
    icons/output.recorder.svg
    icons/output.virtualdevice.svg
    icons/output.wave.svg
    icons/plugin.builtin.svg
    icons/plugin.clap.svg
    icons/plugin.ladspa.svg
    icons/plugin.lv2.svg
    icons/plugin.vst3.svg
    icons/source.application.svg
    icons/source.device.svg
    icons/source.system.svg
)
# **The prototype's own line glyphs**: the few places it draws its own
# icon rather than one an icon theme would have — the header's "+", Run and
# Stop, the theme button and the window buttons (the last two are for our
# own title bar), a menu row's tick and arrow, and the zoom control's "+"
# and "−", an explanation's "i" and the sidebar's Blocks tab. Only in the
# Reverblabr appearance; *Follow system* keeps the icon theme's. AppGlyphs
# names them.
qt_target_qml_sources(reverblabr-ui RESOURCES
    icons/glyph.add.svg
    icons/glyph.blocks.svg
    icons/glyph.check.svg
    icons/glyph.files.svg
    icons/glyph.help.svg
    icons/glyph.info.svg
    icons/glyph.lock.svg
    icons/glyph.menu.svg
    icons/glyph.moon.svg
    icons/glyph.run.svg
    icons/glyph.schedule.svg
    icons/glyph.stop.svg
    icons/glyph.submenu.svg
    icons/glyph.sun.svg
    icons/glyph.virtualdevices.svg
    icons/glyph.window-close.svg
    icons/glyph.window-maximize.svg
    icons/glyph.window-minimize.svg
    icons/glyph.window-restore.svg
    icons/glyph.zoom-in.svg
    icons/glyph.zoom-out.svg
    icons/hearing.compensation16.svg
    icons/hearing.compensation64.svg
    icons/hearing.frequencylowering.svg
    icons/hearing.frequencyraising.svg
    icons/hearing.joinlr.svg
)

# **The Reverblabr appearance's two typefaces ride with it too**: the
# look is the prototype's on every machine, not only on the ones that happen
# to have Noto Sans and JetBrains Mono installed. OFL-1.1; see fonts/README.md.
qt_target_qml_sources(reverblabr-ui RESOURCES
    appicon/16-apps-io.github.reverblabr.Reverblabr.svg
    appicon/sc-apps-io.github.reverblabr.Reverblabr.svg
)

qt_target_qml_sources(reverblabr-ui RESOURCES
    fonts/JetBrainsMono-Medium.ttf
    fonts/JetBrainsMono-SemiBold.ttf
    fonts/NotoSans-Bold.ttf
    fonts/NotoSans-Medium.ttf
    fonts/NotoSans-Regular.ttf
    fonts/NotoSans-SemiBold.ttf
)
target_sources(reverblabr-ui PRIVATE
    appearance.cpp
    appearance.h
    applicationicon.cpp
    applicationicon.h
    appinfo.cpp
    appinfo.h
    appshell.cpp
    appshell.h
    desktopnotifier.cpp
    desktopnotifier.h
    diagnostics.cpp
    diagnostics.h
    fileactions.cpp
    fileactions.h
    audiosystem.cpp
    audiosystem.h
    canvasgrid.cpp
    canvasgrid.h
    compressormodes.cpp
    compressormodes.h
    designpalette.h
    errorsurface.cpp
    errorsurface.h
    globalshortcutregistry.cpp
    globalshortcutregistry.h
    globalshortcuts.cpp
    globalshortcuts.h
    blockcatalogue.cpp
    blockcatalogue.h
    blockmodel.cpp
    blockmodel.h
    blockpresentation.cpp
    blockpresentation.h
    dynamicsplot.cpp
    dynamicsplot.h
    equalizerplot.cpp
    equalizerplot.h
    installedplugins.cpp
    installedplugins.h
    inventory.cpp
    inventory.h
    keyboard.cpp
    keyboard.h
    launchrequests.cpp
    launchrequests.h
    meteritem.cpp
    meteritem.h
    meters.cpp
    meters.h
    meterscale.cpp
    meterscale.h
    peakhold.cpp
    peakhold.h
    recentapplications.cpp
    recentapplications.h
    sessionmodel.cpp
    sessionmodel.h
    presets.cpp
    presets.h
    recordinginspector.cpp
    recordinginspector.h
    recordinglist.cpp
    recordinglist.h
    recordingpreview.cpp
    recordingpreview.h
    recordingstatus.cpp
    recordingstatus.h
    broadcaststatus.cpp
    broadcaststatus.h
    mediaplayers.cpp
    mediaplayers.h
    sessionoverview.cpp
    sessionoverview.h
    trayicon.cpp
    trayicon.h
    traystate.cpp
    traystate.h
    sessions.cpp
    sessions.h
    errormessages.cpp
    errormessages.h
    meterhub.cpp
    meterhub.h
    volumescale.cpp
    volumescale.h
    waveformitem.cpp
    waveformitem.h
    vuscale.cpp
    vuscale.h
    wiremodel.cpp
    wiremodel.h
    windowframe.cpp
    windowframe.h
    windowstate.cpp
    windowstate.h
    virtualdevicefilter.cpp
    virtualdevicefilter.h
    virtualdevices.cpp
    virtualdevices.h
    systemdevicewriter.cpp
    systemdevicewriter.h
)
target_include_directories(reverblabr-ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(reverblabr-ui
    PUBLIC
        reverblabr-engine
        # The bit rates a Broadcast block's format takes, from the stream's
        # own table.
        reverblabr-stream
        # The media players a Broadcast block's title can follow.
        reverblabr-mpris
        # What is worth a desktop notification, and its words, shared
        # with the daemon; the notifier's header is one main.cpp includes.
        reverblabr-notify
        KF6::ConfigCore
        KF6::CoreAddons
        KF6::I18n
        # The QML singletons are declared in headers a test may include, so
        # the QML module has to come with them.
        Qt6::Qml
        # The meter item is a QQuickItem, and its header is one a test
        # includes.
        Qt6::Quick
    PRIVATE
        # The appearance asks which control style is in use.
        Qt6::QuickControls2
        # The frameless window's shadow, drawn by the compositor.
        KF6::WindowSystem
        # The Files tab's Open Containing Folder, Open With… and Move to
        # Trash. KIOWidgets is what gives a job the desktop's dialogs:
        # the application chooser, and an error a job reports itself.
        KF6::KIOCore
        KF6::KIOGui
        KF6::KIOWidgets
        # Which window a job's dialogs belong to.
        KF6::GuiAddons
        # Desktop notifications; KNotifications sends them.
        KF6::Notifications
        # The system tray icon.
        KF6::StatusNotifierItem
        # Global shortcuts, in System Settings → Shortcuts.
        KF6::GlobalAccel
        # The all-users helper's action and id; only its header's
        # names are used here.
        reverblabr-helper-action
)
# The all-users helper is asked through KAuth, when the build has it.
if(RB_BUILD_AUTH_HELPER)
    target_link_libraries(reverblabr-ui PRIVATE KF6::AuthCore)
endif()
target_compile_definitions(reverblabr-ui PRIVATE REVERBLABR_KAUTH=$<BOOL:${RB_BUILD_AUTH_HELPER}>)
if(TARGET Qt6::WaylandClientPrivate)
    target_link_libraries(reverblabr-ui PRIVATE Qt6::WaylandClientPrivate)
    target_compile_definitions(reverblabr-ui PRIVATE REVERBLABR_WAYLAND_WINDOW_MENU=1)
else()
    message(STATUS "Qt's Wayland client internals not found: the header's window menu is our own")
endif()

# The Reverblabr control style and the Kirigami platform plugin that
# gives Kirigami its colours under it.
add_subdirectory(style)
add_subdirectory(kirigami)

add_executable(reverblabr
    main.cpp
)
target_link_libraries(reverblabr PRIVATE
    reverblabr-version
    reverblabr-engine
    reverblabr-enginebus
    reverblabr-uiplugin
    reverblabr-styleplugin
    Qt6::Widgets
    Qt6::Quick
    Qt6::QuickControls2
    # The appearance is read before the window exists, and its
    # header names the settings file it reads.
    KF6::ConfigCore
    KF6::CoreAddons
    KF6::I18n
    KF6::I18nQml
    # The desktop notifier is made here, beside the engine it follows.
    reverblabr-notify
    # One Reverblabr a desktop: a second launch hands its arguments to
    # the first, and its activation token, which KWindowSystem passes on.
    KF6::DBusAddons
    KF6::WindowSystem
)

install(TARGETS reverblabr ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
# **Without these the application has no identity.** The desktop entry is
# what the portal, the task manager and the Wayland app id all look up by
# the name KAboutData carries; with no entry installed, the portal says "app
# info not found" and the window has no icon and no menu entry.
# Both installed with their translations merged in: the launcher and
# a software centre show them before the application has run.
rb_translate_data_file(reverblabr DESKTOP
    ${CMAKE_CURRENT_SOURCE_DIR}/io.github.reverblabr.Reverblabr.desktop
    ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Reverblabr.desktop)
rb_translate_data_file(reverblabr XML
    ${CMAKE_CURRENT_SOURCE_DIR}/io.github.reverblabr.Reverblabr.metainfo.xml
    ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Reverblabr.metainfo.xml)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Reverblabr.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/io.github.reverblabr.Reverblabr.metainfo.xml DESTINATION ${KDE_INSTALL_METAINFODIR})
# **The application's icon**, ours, under the application's id in the
# hicolor theme: the full drawing for every size, and one made for 16 px.
# The same two files are built into the application (above), for a build
# that has not been installed.
ecm_install_icons(ICONS
    appicon/16-apps-io.github.reverblabr.Reverblabr.svg
    appicon/sc-apps-io.github.reverblabr.Reverblabr.svg
    DESTINATION ${KDE_INSTALL_ICONDIR}
    THEME hicolor)

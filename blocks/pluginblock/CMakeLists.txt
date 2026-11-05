# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Test Plugin: a plugin built in, for checking that plugins run.
# LV2: an LV2 plugin installed on this machine.
# LADSPA: a LADSPA plugin installed on this machine.
# CLAP: a CLAP plugin installed on this machine.
# VST3: a VST3 plugin installed on this machine.

rb_add_block(pluginblock
    # A plugin's help is its maker's: the help window says what the
    # library knows of it.
    NO_HELP_PAGE
    ENGINE
        pluginblock.cpp
        pluginblock.h
    UI
        ui/PluginFace.qml
        ui/PluginPanel.qml
    ICONS
        ui/plugin.builtin.svg
        ui/plugin.clap.svg
        ui/plugin.ladspa.svg
        ui/plugin.lv2.svg
        ui/plugin.vst3.svg
    TESTS
        tests/test_pluginblock.cpp
    QML_TESTS
        tests/tst_plugin.qml
)

# A plugin block runs its plugin in a host of its own, through the protocol
# they share.
target_link_libraries(reverblabr-blocks PRIVATE reverblabr-pluginproto)

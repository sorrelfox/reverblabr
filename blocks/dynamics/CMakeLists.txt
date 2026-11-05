# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Dynamics Processor: holds loud sound back and lifts quiet sound.

rb_add_block(dynamics
    ENGINE
        dynamics.cpp
        dynamics.h
    UI
        ui/DynamicsFace.qml
        ui/DynamicsHelp.qml
        ui/DynamicsPanel.qml
    ICONS
        ui/effect.dynamics.svg
    PRESETS
        presets/effect.dynamics/10-default.json
        presets/effect.dynamics/20-gentleLeveling.json
        presets/effect.dynamics/30-voice.json
        presets/effect.dynamics/40-noiseGate.json
        presets/effect.dynamics/50-hardLimiting.json
    TESTS
        tests/test_dynamics.cpp
    QML_TESTS
        tests/tst_dynamicspanel.qml
)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# 10-Band EQ: shapes the tone with ten fixed bands.

rb_add_block(equalizer10
    ENGINE
        equalizer10.cpp
        equalizer10.h
    UI
        ui/EqualizerBands.qml
        ui/EqualizerFace.qml
        ui/EqualizerHelp.qml
        ui/EqualizerPanel.qml
    ICONS
        ui/effect.equalizer10.svg
    PRESETS
        presets/effect.equalizer10/010-flat.json
        presets/effect.equalizer10/020-bassBoost.json
        presets/effect.equalizer10/030-lessBass.json
        presets/effect.equalizer10/040-trebleBoost.json
        presets/effect.equalizer10/050-lessTreble.json
        presets/effect.equalizer10/060-vocalPresence.json
        presets/effect.equalizer10/070-podcastVoice.json
        presets/effect.equalizer10/080-loudness.json
        presets/effect.equalizer10/090-headphones.json
        presets/effect.equalizer10/100-laptopSpeakers.json
        presets/effect.equalizer10/110-warm.json
        presets/effect.equalizer10/120-bright.json
        presets/effect.equalizer10/130-telephone.json
        presets/effect.equalizer10/140-oldRadio.json
        presets/effect.equalizer10/150-rumbleCut.json
        presets/effect.equalizer10/160-hissCut.json
        presets/effect.equalizer10/170-lessMud.json
        presets/effect.equalizer10/180-lessHarshness.json
        presets/effect.equalizer10/190-air.json
        presets/effect.equalizer10/200-smile.json
    TESTS
        tests/test_equalizer10.cpp
    QML_TESTS
        tests/tst_equalizer10.qml
)

# SPDX-FileCopyrightText: 2026 Reverblabr contributors
# SPDX-License-Identifier: MPL-2.0
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Spectrum: shows how much of each frequency there is at this point in a lane.

rb_add_block(spectrummeter
    ENGINE
        spectrummeter.cpp
        spectrummeter.h
    UI
        ui/ScopeChoiceRow.qml
        ui/SpectrumFace.qml
        ui/SpectrumHelp.qml
        ui/spectrumitem.cpp
        ui/spectrumitem.h
        ui/SpectrumPanel.qml
        ui/SpectrumScope.qml
        ui/SpectrumState.qml
        ui/SpectrumUi.qml
        ui/SpectrumWords.qml
    ICONS
        ui/meter.spectrum.svg
    PRESETS
        presets/meter.spectrum/10-musicBalance.json
        presets/meter.spectrum/20-fineDetail.json
        presets/meter.spectrum/30-quickLook.json
        presets/meter.spectrum/40-ledBars.json
    TESTS
        tests/test_spectrummeter.cpp
    UI_TESTS
        tests/test_spectrumitem.cpp
    QML_TESTS
        tests/tst_spectrum.qml
)

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick

/// **Every block's interface, by type id** (ADR-0035). What a block's face,
/// panel and help page are is in its folder, blocks/<folder>/ui/<Name>Ui.qml,
/// and this is the one list of them: a new block adds its line here. A
/// block that is not here gets the generic face and panel, and the help
/// window says what the library knows of it.
QtObject {
    id: register

    readonly property list<BlockUi> blocks: [
        AmplifyUi {},
        ApplicationSourceUi {},
        AutoLevelUi {},
        BalanceUi {},
        BassTrebleUi {},
        BroadcastUi {},
        ChannelsUi {},
        DeclickUi {},
        DehumUi {},
        DenoiseUi {},
        DuckingUi {},
        DynamicsUi {},
        EchoUi {},
        EqualizerUi {},
        FirFilterUi {},
        FrequencyLoweringUi {},
        FrequencyRaisingUi {},
        HearingCompensation16Ui {},
        HearingCompensation64Ui {},
        HighPassUi {},
        InputDeviceUi {},
        InputSwitchUi {},
        JoinLrUi {},
        LimiterUi {},
        LiveStreamUi {},
        LowPassUi {},
        MixerUi {},
        ModelUi {},
        MonoEnhancerUi {},
        MultibandCompressorUi {},
        NightLevelUi {},
        OneEarMixUi {},
        OutputDeviceUi {},
        PanUi {},
        ParametricEqUi {},
        PeakRmsUi {},
        PitchShiftUi {},
        PluginUi {},
        RecorderUi {},
        ReverbUi {},
        SimpleCompressorUi {},
        SoundSideUi {},
        SpectrumUi {},
        SpeechDenoiseUi {},
        SpeechNowUi {},
        SuddenSoundsUi {},
        SyncUi {},
        SystemAudioUi {},
        TimeShiftUi {},
        VoiceCompressorUi {},
        VoiceForwardUi {},
        VoiceGaugeUi {},
        VolumeUi {},
        VuUi {},
        WaveformUi {},
        WaveUi {}
    ]

    /// Type id to the block's entry.
    readonly property var byType: {
        const entries = {};
        for (const block of register.blocks) {
            for (const typeId of block.typeIds) {
                entries[typeId] = block;
            }
        }
        return entries;
    }

    /// The type ids with a help page of their own.
    readonly property var typesWithHelp: Object.keys(register.byType)
        .filter(typeId => register.byType[typeId].help !== null)

    function faceFor(typeId: string): Component {
        return register.byType[typeId]?.face ?? null;
    }

    function panelFor(typeId: string): Component {
        return register.byType[typeId]?.panel ?? null;
    }

    function pageFor(typeId: string): Component {
        return register.byType[typeId]?.help ?? null;
    }
}

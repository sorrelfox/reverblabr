// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.ki18n

// The blocks' help pages, by type id, and the one window they are shown in
// (docs/adr/0029-block-help.md).
//
// **One window for every block**, made the first time somebody asks for
// help and then kept: asking about another block turns its page, rather
// than opening a second window somebody then has to find and close. It is
// a window of its own, not a dialog over the canvas, so it can sit beside
// the settings it explains while they are used.
//
// **A block with no page yet still gets the window**, with what the
// library says of it and the way to the handbook; tools.help-pages lists
// the blocks whose page is still to write.
QtObject {
    id: help

    /// The window, once it has been asked for; null until then.
    property var window: null

    // **The parts every page has, named once**: one message each, so the
    // same heading is the same words on every page and in every language.
    readonly property string aboutTitle: KI18n.i18nc("@title a part of a block's help page", "What it does")
    readonly property string settingsTitle: KI18n.i18nc("@title a part of a block's help page", "Settings")
    readonly property string whenTitle: KI18n.i18nc("@title a part of a block's help page", "When to use it")
    readonly property string tipsTitle: KI18n.i18nc("@title a part of a block's help page", "Tips")

    readonly property Component volume: Component {
        VolumeHelp {}
    }

    readonly property Component peakRms: Component {
        PeakRmsHelp {}
    }

    readonly property Component recorder: Component {
        RecorderHelp {}
    }

    readonly property Component applicationSource: Component {
        ApplicationSourceHelp {}
    }

    readonly property Component autoLevel: Component {
        AutoLevelHelp {}
    }

    readonly property Component balance: Component {
        BalanceHelp {}
    }

    readonly property Component bassTreble: Component {
        BassTrebleHelp {}
    }

    readonly property Component broadcast: Component {
        BroadcastHelp {}
    }

    readonly property Component channels: Component {
        ChannelsHelp {}
    }

    readonly property Component declick: Component {
        DeclickHelp {}
    }

    readonly property Component dehum: Component {
        DehumHelp {}
    }

    readonly property Component denoise: Component {
        DenoiseHelp {}
    }

    readonly property Component ducking: Component {
        DuckingHelp {}
    }

    readonly property Component dynamics: Component {
        DynamicsHelp {}
    }

    readonly property Component echo: Component {
        EchoHelp {}
    }

    readonly property Component equalizer10: Component {
        EqualizerHelp {}
    }

    readonly property Component firFilter: Component {
        FirFilterHelp {}
    }

    readonly property Component frequencyLowering: Component {
        FrequencyLoweringHelp {}
    }

    readonly property Component frequencyRaising: Component {
        FrequencyRaisingHelp {}
    }

    readonly property Component hearingCompensation16: Component {
        HearingCompensation16Help {}
    }

    readonly property Component hearingCompensation64: Component {
        HearingCompensation64Help {}
    }

    readonly property Component highPass: Component {
        HighPassHelp {}
    }

    readonly property Component inputDevice: Component {
        InputDeviceHelp {}
    }

    readonly property Component inputSwitch: Component {
        InputSwitchHelp {}
    }

    readonly property Component joinLr: Component {
        JoinLrHelp {}
    }

    readonly property Component limiter: Component {
        LimiterHelp {}
    }

    readonly property Component liveStream: Component {
        LiveStreamHelp {}
    }

    readonly property Component lowPass: Component {
        LowPassHelp {}
    }

    readonly property Component mixer: Component {
        MixerHelp {}
    }

    readonly property Component monoEnhancer: Component {
        MonoEnhancerHelp {}
    }

    readonly property Component multibandCompressor: Component {
        MultibandCompressorHelp {}
    }

    readonly property Component outputDevice: Component {
        OutputDeviceHelp {}
    }

    readonly property Component pan: Component {
        PanHelp {}
    }

    readonly property Component parametricEq: Component {
        ParametricEqHelp {}
    }

    readonly property Component pitchShift: Component {
        PitchShiftHelp {}
    }

    readonly property Component reverb: Component {
        ReverbHelp {}
    }

    readonly property Component simpleCompressor: Component {
        SimpleCompressorHelp {}
    }

    readonly property Component speechDenoise: Component {
        SpeechDenoiseHelp {}
    }

    readonly property Component sync: Component {
        SyncHelp {}
    }

    readonly property Component systemAudio: Component {
        SystemAudioHelp {}
    }

    readonly property Component timeShift: Component {
        TimeShiftHelp {}
    }

    readonly property Component vu: Component {
        VuHelp {}
    }

    readonly property Component wave: Component {
        WaveHelp {}
    }

    /// Type id to page. A block's help adds its entry here, and comes off
    /// the list of those still to write (tests/tools/help-pending.txt).
    readonly property var pages: ({
        "advanced.declick": help.declick,
        "advanced.dehum": help.dehum,
        "advanced.denoise": help.denoise,
        "advanced.ducking": help.ducking,
        "advanced.firfilter": help.firFilter,
        "advanced.inputswitch": help.inputSwitch,
        "advanced.mixer": help.mixer,
        "advanced.parametriceq": help.parametricEq,
        "advanced.speechdenoise": help.speechDenoise,
        "advanced.sync": help.sync,
        "advanced.timeshift": help.timeShift,
        "effect.autolevel": help.autoLevel,
        "effect.balance": help.balance,
        "effect.basstreble": help.bassTreble,
        "effect.channels": help.channels,
        "effect.dynamics": help.dynamics,
        "effect.echo": help.echo,
        "effect.equalizer10": help.equalizer10,
        "effect.highpass": help.highPass,
        "effect.limiter": help.limiter,
        "effect.lowpass": help.lowPass,
        "effect.monoenhancer": help.monoEnhancer,
        "effect.multibandcompressor": help.multibandCompressor,
        "effect.pan": help.pan,
        "effect.pitchshift": help.pitchShift,
        "effect.reverb": help.reverb,
        "effect.simplecompressor": help.simpleCompressor,
        "effect.volume": help.volume,
        "hearing.compensation16": help.hearingCompensation16,
        "hearing.compensation64": help.hearingCompensation64,
        "hearing.frequencylowering": help.frequencyLowering,
        "hearing.frequencyraising": help.frequencyRaising,
        "hearing.joinlr": help.joinLr,
        "meter.peakrms": help.peakRms,
        "meter.vu": help.vu,
        "output.broadcast": help.broadcast,
        "output.device": help.outputDevice,
        "output.livestream": help.liveStream,
        "output.recorder": help.recorder,
        "output.wave": help.wave,
        "source.application": help.applicationSource,
        "source.device": help.inputDevice,
        "source.system": help.systemAudio
    })

    function pageFor(typeId: string): Component {
        return help.pages[typeId] ?? null;
    }

    readonly property Component windowComponent: Component {
        BlockHelpWindow {}
    }

    /// Shows the help for \a typeId, making the window the first time.
    /// \a near is the window the request came from: the help opens beside
    /// it on its screen, where the compositor lets an application say.
    function show(typeId: string, near: var): var {
        const first = help.window === null;
        if (first) {
            help.window = help.windowComponent.createObject(null);
        }
        help.window.typeId = typeId;
        if (first) {
            help.window.placeBeside(near ?? null);
        }
        help.window.show();
        help.window.raise();
        help.window.requestActivate();
        return help.window;
    }
}

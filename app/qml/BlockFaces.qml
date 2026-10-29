// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.ki18n

// What each block shows on its tile, by type id.
//
// **A face is not a small panel.** A panel is read; a face is glanced at
// while something else is being listened to, so it says the one thing the
// block is about and offers the one control worth reaching for without
// opening anything.
//
// The counterpart of BlockPanels: a block with no entry here falls back to
// what its `face` kind says, which the canvas decides. Each block's own UI
// task adds its entry.
QtObject {
    id: faces

    /// The two device blocks share one face, and the register settles
    /// which of them it is. **"Plays" or "records" is knowable from the
    /// type id**, so it is answered here once rather than by an `if` on the
    /// type id wherever a tile is drawn — and a property bound in the
    /// register is a property a Loader never has to hand over.
    readonly property Component outputDevice: Component {
        DeviceFace {
            output: true
        }
    }

    readonly property Component inputDevice: Component {
        DeviceFace {
            output: false
        }
    }

    /// The Application face: the program's own icon and name, and whether
    /// anything is coming out of it — neither of which any generic face
    /// could know to ask the inventory for.
    readonly property Component applicationSource: Component {
        ApplicationSourceFace {}
    }

    /// The System Audio face: the icon kind the block declares, with the
    /// one line the generic icon face cannot know — how much of the machine
    /// the block has been told to take.
    readonly property Component systemAudio: Component {
        SystemAudioFace {}
    }

    readonly property Component volume: Component {
        VolumeFace {}
    }

    /// The Balance face: where it is set, "C" or "L 30", and a slider.
    readonly property Component balance: Component {
        StereoPositionFace {
            typeId: "effect.balance"
            paramId: "balance"
            caption: KI18n.i18nc("@label what the setting on a Balance tile is", "balance")
            sliderName: KI18n.i18nc("@label:slider", "Balance")
        }
    }

    readonly property Component peakRms: Component {
        PeakRmsFace {}
    }

    /// The VU face: two small dials, as the design draws its VU tile.
    readonly property Component vu: Component {
        VuFace {}
    }

    /// The Pan face: the same drawing as Balance's, for where one sound is
    /// placed rather than which side is trimmed.
    readonly property Component pan: Component {
        StereoPositionFace {
            typeId: "effect.pan"
            paramId: "pan"
            caption: KI18n.i18nc("@label what the setting on a Pan tile is", "pan")
            sliderName: KI18n.i18nc("@label:slider", "Pan")
        }
    }

    /// The Channels face: the action, and where each side comes from.
    readonly property Component channels: Component {
        ChannelsFace {}
    }

    /// The Mono Enhancer face: where the width starts, and a slider.
    readonly property Component monoEnhancer: Component {
        MonoEnhancerFace {}
    }

    /// The Auto Level face: the boost it is applying, and how far
    /// that is towards the most it may add.
    readonly property Component autoLevel: Component {
        AutoLevelFace {}
    }

    /// The Limiter face: the gain it is taking off, or its ceiling
    /// while nothing plays.
    readonly property Component limiter: Component {
        LimiterFace {}
    }

    /// The Echo face: the time between the repeats, and a slider.
    readonly property Component echo: Component {
        EchoFace {}
    }

    /// The Pitch Shift face: the shift, "+3 st", and a slider of
    /// whole semitones.
    readonly property Component pitchShift: Component {
        PitchShiftFace {}
    }

    /// The Reverb face: the decay, the mix, and the decay's slider.
    readonly property Component reverb: Component {
        ReverbFace {}
    }

    /// The Hearing Compensation face: the lift for soft and for
    /// average sound across frequency, small.
    readonly property Component hearingCompensation: Component {
        HearingCompensationFace {}
    }

    /// The same for Hearing Compensation 64, from the curves through
    /// its points.
    readonly property Component hearingCompensation64: Component {
        HearingCompensationFace {
            typeId: "hearing.compensation64"
        }
    }

    /// The Declick face: the threshold and a slider, as a value tile.
    readonly property Component declick: Component {
        DeclickFace {}
    }

    /// The Denoise face: whether it has a noise to take out, and how
    /// much of it was learned.
    readonly property Component denoise: Component {
        DenoiseFace {}
    }

    /// The FIR Filter face: the impulse response's file and its
    /// length.
    readonly property Component firFilter: Component {
        FirFilterFace {}
    }

    /// The Speech Denoise face: the block's icon over the model it
    /// runs, or that it only works at 48 kHz.
    readonly property Component speechDenoise: Component {
        SpeechDenoiseFace {}
    }

    /// The Dynamics face: the block declares a curve face, and the curve is
    /// what it has to say for itself.
    readonly property Component dynamics: Component {
        DynamicsFace {}
    }

    readonly property Component wave: Component {
        WaveFace {}
    }

    /// The Recorder: the format, how it records, a REC pill with the
    /// time while it records, and Pause and Split.
    readonly property Component recorder: Component {
        RecorderFace {}
    }

    /// The Input Switch face: A and B, the one passing chosen, and a
    /// click on the other to switch.
    readonly property Component inputSwitch: Component {
        InputSwitchFace {}
    }

    /// The Mixer face: the master and how many inputs are in use.
    readonly property Component mixer: Component {
        MixerFace {}
    }

    /// The Ducking face: where it starts, how far down it takes the
    /// background, and how far down the background is while it is.
    readonly property Component ducking: Component {
        DuckingFace {}
    }

    /// The Simple Compressor face: the mode, and how hard it works.
    readonly property Component simpleCompressor: Component {
        SimpleCompressorFace {}
    }

    /// The Multiband Compressor face: each band's gain reduction
    /// hanging in its well, and where the bands meet.
    readonly property Component multibandCompressor: Component {
        MultibandCompressorFace {}
    }

    /// The Bass & Treble face: both ends' lift or cut, a slider for each.
    readonly property Component bassTreble: Component {
        BassTrebleFace {}
    }

    /// The 10-Band EQ face: its ten bands as ten bars.
    readonly property Component equalizer10: Component {
        EqualizerFace {}
    }

    /// The Low Pass Filter face: the cutoff, and a slider for it.
    readonly property Component lowPass: Component {
        CutoffFace {
            typeId: "effect.lowpass"
        }
    }

    /// The High Pass Filter's, the same with the other side kept.
    readonly property Component highPass: Component {
        CutoffFace {
            typeId: "effect.highpass"
        }
    }

    /// The Sync face: the delay and a slider, as the design draws a value
    /// tile.
    readonly property Component sync: Component {
        SyncFace {}
    }

    /// The Time Shift face: where the play head is, and pause and ten
    /// seconds either way under it.
    readonly property Component timeShift: Component {
        TimeShiftFace {}
    }

    /// The Parametric EQ face: its response, small.
    readonly property Component parametricEq: Component {
        ParametricEqFace {}
    }

    /// A plugin's face: which plugin, and what its host is doing.
    readonly property Component plugin: Component {
        PluginFace {}
    }

    /// The Dehum face: the base it takes out and how much, a slider each.
    readonly property Component dehum: Component {
        DehumFace {}
    }

    /// The Frequency Lowering face: its first move, how many more,
    /// and whether it is moving sound now.
    readonly property Component frequencyLowering: Component {
        FrequencyLoweringFace {}
    }

    /// The Frequency Raising face: the same as Frequency Lowering's,
    /// its moves going up.
    readonly property Component frequencyRaising: Component {
        FrequencyRaisingFace {}
    }

    /// Type id to Component.
    readonly property var components: ({
        "advanced.dehum": faces.dehum,
        "advanced.parametriceq": faces.parametricEq,
        "hearing.frequencylowering": faces.frequencyLowering,
        "hearing.frequencyraising": faces.frequencyRaising,
        "output.device": faces.outputDevice,
        "source.application": faces.applicationSource,
        "source.device": faces.inputDevice,
        "source.system": faces.systemAudio,
        "advanced.declick": faces.declick,
        "hearing.compensation16": faces.hearingCompensation,
        "hearing.compensation64": faces.hearingCompensation64,
        "advanced.denoise": faces.denoise,
        "advanced.ducking": faces.ducking,
        "advanced.firfilter": faces.firFilter,
        "advanced.inputswitch": faces.inputSwitch,
        "advanced.mixer": faces.mixer,
        "advanced.speechdenoise": faces.speechDenoise,
        "effect.autolevel": faces.autoLevel,
        "effect.balance": faces.balance,
        "effect.basstreble": faces.bassTreble,
        "effect.channels": faces.channels,
        "effect.dynamics": faces.dynamics,
        "effect.echo": faces.echo,
        "effect.pitchshift": faces.pitchShift,
        "effect.reverb": faces.reverb,
        "effect.monoenhancer": faces.monoEnhancer,
        "effect.multibandcompressor": faces.multibandCompressor,
        "effect.pan": faces.pan,
        "effect.equalizer10": faces.equalizer10,
        "effect.simplecompressor": faces.simpleCompressor,
        "effect.highpass": faces.highPass,
        "effect.limiter": faces.limiter,
        "effect.lowpass": faces.lowPass,
        "effect.volume": faces.volume,
        "meter.peakrms": faces.peakRms,
        "meter.vu": faces.vu,
        "advanced.sync": faces.sync,
        "advanced.timeshift": faces.timeShift,
        "output.recorder": faces.recorder,
        "plugin.builtin": faces.plugin,
        "plugin.lv2": faces.plugin,
        "plugin.ladspa": faces.plugin,
        "plugin.clap": faces.plugin,
        "plugin.vst3": faces.plugin,
        "output.wave": faces.wave
    })

    function faceFor(typeId: string): Component {
        return components[typeId] ?? null;
    }
}

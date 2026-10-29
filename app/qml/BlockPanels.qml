// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

pragma Singleton

import QtQuick
import org.kde.ki18n

// The blocks that have a panel of their own, by type id.
//
// **Most blocks do not need one.** The generic panel is generated from the
// parameter descriptors and is right for anything whose settings are a list
// of settings; a panel of its own is for a block whose settings are a
// *shape* — a transfer curve, a set of mixer strips, a transport.
//
// It is also the accessible fallback: a custom panel that a screen reader
// cannot follow is a block somebody cannot use, and the generic one always
// can be reached from the panel's own menu.
QtObject {
    id: panels

    /// The Output Device and the Input Device panels: a device reference is
    /// a live list, a volume is a percentage, and a channel is one of the
    /// ones *that* device has — none of which the generic panel can know.
    readonly property Component outputDevice: Component {
        OutputDevicePanel {}
    }

    readonly property Component inputDevice: Component {
        InputDevicePanel {}
    }

    /// The Application panel: which program the block listens to, which is
    /// a match rule chosen from what is playing rather than three engine
    /// spellings typed into three boxes.
    readonly property Component applicationSource: Component {
        ApplicationSourcePanel {}
    }

    /// The System Audio panel: an exclusion list of match rules, which is
    /// not a list of settings — the generic panel would offer a row of
    /// strings to type, and the strings are the engine's spelling rather
    /// than anything a person knows.
    readonly property Component systemAudio: Component {
        SystemAudioPanel {}
    }

    /// The Volume panel: a percentage over an amplitude, and a setting that
    /// moves the top of a slider — neither of which the generic panel can
    /// know about.
    readonly property Component volume: Component {
        VolumePanel {}
    }

    /// The Balance panel: a side and how far, "L 30", which the
    /// generic panel would write as a signed number.
    readonly property Component balance: Component {
        StereoPositionPanel {
            paramId: "balance"
            title: KI18n.i18nc("@label:slider", "Balance")
        }
    }

    /// The Pan panel: Balance's panel, moving the Pan's setting.
    readonly property Component pan: Component {
        StereoPositionPanel {
            paramId: "pan"
            title: KI18n.i18nc("@label:slider", "Pan")
        }
    }

    /// The Channels panel: the action by name, and a line saying
    /// what it does, which the generic panel's list could not.
    readonly property Component channels: Component {
        ChannelsPanel {}
    }

    /// The Mono Enhancer panel: a crossover on a logarithmic slider,
    /// and a line saying what the block is for.
    readonly property Component monoEnhancer: Component {
        MonoEnhancerPanel {}
    }

    /// The Auto Level panel: a switch and a sentence, with the
    /// target and the maximum folded under "Advanced".
    readonly property Component autoLevel: Component {
        AutoLevelPanel {}
    }

    /// The Limiter panel: the ceiling in dBTP or dBFS, a logarithmic
    /// release, the true-peak switch and the reduction as a reading.
    readonly property Component limiter: Component {
        LimiterPanel {}
    }

    /// The Echo panel: time, feedback, tone and mix, the time and
    /// the tone on logarithmic sliders.
    readonly property Component echo: Component {
        EchoPanel {}
    }

    /// The Pitch Shift panel: semitones and cents, each a slider with
    /// its number.
    readonly property Component pitchShift: Component {
        PitchShiftPanel {}
    }

    /// The Reverb panel: pre-delay, size, decay, damping, width and
    /// mix, the decay logarithmic.
    readonly property Component reverb: Component {
        ReverbPanel {}
    }

    /// The Hearing Compensation panel: the gain and levels views, a
    /// handle on every band, the band inspector, the audiogram and its fit.
    readonly property Component hearingCompensation: Component {
        HearingCompensationPanel {}
    }

    /// Hearing Compensation 64's: the same, with a handle on each
    /// control point instead of each band.
    readonly property Component hearingCompensation64: Component {
        HearingCompensationPanel {
            typeId: "hearing.compensation64"
        }
    }

    /// The Declick panel: the threshold, and how many clicks were
    /// taken out while the session runs.
    readonly property Component declick: Component {
        DeclickPanel {}
    }

    /// The Denoise panel: the state and the Learn or Forget button,
    /// which no generated panel could offer, and its two settings.
    readonly property Component denoise: Component {
        DenoisePanel {}
    }

    /// The FIR Filter panel: a file to choose, and what the running
    /// block made of it, which no generated panel could show.
    readonly property Component firFilter: Component {
        FirFilterPanel {}
    }

    /// The Speech Denoise panel: whether it hears speech, which no
    /// generated panel could show, and its two settings.
    readonly property Component speechDenoise: Component {
        SpeechDenoisePanel {}
    }

    /// The Dynamics panel: a transfer curve with handles on it, which is a
    /// shape rather than a list of settings — and the four handles are four
    /// of the twelve parameters, so a generated row per parameter would say
    /// the same things twice.
    readonly property Component dynamics: Component {
        DynamicsPanel {}
    }

    /// The Peak/RMS panel: two meters, the numbers under them, and a hold
    /// that is measured in wall-clock time rather than in parameters.
    readonly property Component peakRms: Component {
        PeakRmsPanel {}
    }

    /// The VU panel: two dials with their marks, and which level
    /// reads 0 VU — a dial is a shape, not a list of settings.
    readonly property Component vu: Component {
        VuPanel {}
    }

    /// The Wave panel: words for the waveform and the side, and a
    /// frequency on a logarithmic slider with a box for an exact one.
    readonly property Component wave: Component {
        WavePanel {}
    }

    /// The Recorder: what it is doing, Pause and Split file, and its
    /// General, Tags and Limits sections.
    readonly property Component recorder: Component {
        RecorderPanel {}
    }

    /// The Broadcast block: what its stream is doing, and its Setup,
    /// Metadata and Status tabs.
    readonly property Component broadcast: Component {
        BroadcastPanel {}
    }

    /// The Live Stream block: what its stream is doing, and its Setup,
    /// Picture and Status tabs.
    readonly property Component liveStream: Component {
        LiveStreamPanel {}
    }

    /// The Input Switch panel: the two inputs and a switch standing
    /// between them.
    readonly property Component inputSwitch: Component {
        InputSwitchPanel {}
    }

    /// The Mixer panel: five strips, a master, fades and scenes.
    readonly property Component mixer: Component {
        MixerPanel {}
    }

    /// The Ducking panel: the threshold and the ducked volume, and
    /// the timing folded away.
    readonly property Component ducking: Component {
        DuckingPanel {}
    }

    /// The Simple Compressor panel: one list of modes, what the chosen
    /// one is for, and the numbers it sets, to read.
    readonly property Component simpleCompressor: Component {
        SimpleCompressorPanel {}
    }

    /// The Multiband Compressor panel: the bands with their splits
    /// to drag, the picked band's curve, and its controls.
    readonly property Component multibandCompressor: Component {
        MultibandCompressorPanel {}
    }

    /// The Bass & Treble panel: two sliders that say their lift or
    /// cut with a sign, and go back to 0 dB on a double-click.
    readonly property Component bassTreble: Component {
        BassTreblePanel {}
    }

    /// The 10-Band EQ panel: ten upright sliders under a dB scale,
    /// the preamp, keeping the level, and Flatten.
    readonly property Component equalizer10: Component {
        EqualizerPanel {}
    }

    /// The Low Pass Filter panel: a cutoff on a logarithmic slider
    /// with a box for an exact one, and the slope in words.
    readonly property Component lowPass: Component {
        CutoffPanel {
            typeId: "effect.lowpass"
        }
    }

    /// The High Pass Filter's, the same with the other side kept.
    readonly property Component highPass: Component {
        CutoffPanel {
            typeId: "effect.highpass"
        }
    }

    /// The Sync panel: the delay as a number to type to a hundredth
    /// of a millisecond, a square-law slider, and what it comes to in
    /// samples while the session runs.
    readonly property Component sync: Component {
        SyncPanel {}
    }

    /// The Time Shift panel: the position as a scrubber, the whole
    /// transport, and how many minutes to keep.
    readonly property Component timeShift: Component {
        TimeShiftPanel {}
    }

    /// The Parametric EQ panel: the response with a handle on each
    /// band, a chip to pick a band by, and the picked band's settings.
    readonly property Component parametricEq: Component {
        ParametricEqPanel {}
    }

    /// A plugin's panel: which plugin, and what its host is doing.
    readonly property Component plugin: Component {
        PluginPanel {}
    }

    /// The Dehum panel: 50 or 60 Hz, the fine tuning, the amount in
    /// decibels, and how many harmonics.
    readonly property Component dehum: Component {
        DehumPanel {}
    }

    /// The Frequency Lowering panel: the moves as arrows or as a
    /// map over the sound before and after, and the picked move's settings.
    readonly property Component frequencyLowering: Component {
        FrequencyLoweringPanel {}
    }

    /// The Frequency Raising panel: Frequency Lowering's, its moves
    /// going up, with harmonics as a third way.
    readonly property Component frequencyRaising: Component {
        FrequencyRaisingPanel {}
    }

    /// Type id to Component. Each block's own UI task adds its entry here;
    /// a block with no entry gets the generic panel, which is not a lesser
    /// one.
    readonly property var components: ({
        "advanced.dehum": panels.dehum,
        "advanced.parametriceq": panels.parametricEq,
        "hearing.frequencylowering": panels.frequencyLowering,
        "hearing.frequencyraising": panels.frequencyRaising,
        "output.device": panels.outputDevice,
        "source.application": panels.applicationSource,
        "source.device": panels.inputDevice,
        "source.system": panels.systemAudio,
        "advanced.declick": panels.declick,
        "hearing.compensation16": panels.hearingCompensation,
        "hearing.compensation64": panels.hearingCompensation64,
        "advanced.denoise": panels.denoise,
        "advanced.ducking": panels.ducking,
        "advanced.firfilter": panels.firFilter,
        "advanced.inputswitch": panels.inputSwitch,
        "advanced.mixer": panels.mixer,
        "advanced.speechdenoise": panels.speechDenoise,
        "effect.autolevel": panels.autoLevel,
        "effect.balance": panels.balance,
        "effect.basstreble": panels.bassTreble,
        "effect.channels": panels.channels,
        "effect.dynamics": panels.dynamics,
        "effect.echo": panels.echo,
        "effect.pitchshift": panels.pitchShift,
        "effect.reverb": panels.reverb,
        "effect.monoenhancer": panels.monoEnhancer,
        "effect.multibandcompressor": panels.multibandCompressor,
        "effect.pan": panels.pan,
        "effect.equalizer10": panels.equalizer10,
        "effect.simplecompressor": panels.simpleCompressor,
        "effect.highpass": panels.highPass,
        "effect.limiter": panels.limiter,
        "effect.lowpass": panels.lowPass,
        "effect.volume": panels.volume,
        "meter.peakrms": panels.peakRms,
        "meter.vu": panels.vu,
        "advanced.sync": panels.sync,
        "advanced.timeshift": panels.timeShift,
        "output.broadcast": panels.broadcast,
        "output.livestream": panels.liveStream,
        "output.recorder": panels.recorder,
        "plugin.builtin": panels.plugin,
        "plugin.lv2": panels.plugin,
        "plugin.ladspa": panels.plugin,
        "plugin.clap": panels.plugin,
        "output.wave": panels.wave
    })

    function panelFor(typeId: string): Component {
        return components[typeId] ?? null;
    }
}

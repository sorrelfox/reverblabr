// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/builtinblocks.h"

#include "blocks/amplify/amplify.h"
#include "blocks/applicationsource/applicationsource.h"
#include "blocks/autolevel/autolevel.h"
#include "blocks/balance/balance.h"
#include "blocks/basstreble/basstreble.h"
#ifdef RB_HAVE_BROADCAST
#include "blocks/broadcast/broadcast.h"
#endif
#include "blocks/channels/channels.h"
#ifdef RB_HAVE_LIVESTREAM
#include "blocks/broadcast/livestream.h"
#endif
#include "blocks/cutofffilter/highpassfilter.h"
#include "blocks/cutofffilter/lowpassfilter.h"
#include "blocks/declick/declick.h"
#include "blocks/dehum/dehum.h"
#include "blocks/denoise/denoise.h"
#include "blocks/ducking/ducking.h"
#include "blocks/dynamics/dynamics.h"
#include "blocks/echo/echo.h"
#include "blocks/equalizer10/equalizer10.h"
#include "blocks/firfilter/firfilter.h"
#include "blocks/frequencymove/frequencylowering.h"
#include "blocks/frequencymove/frequencyraising.h"
#include "blocks/hearingcompensation/hearingcompensation.h"
#include "blocks/inputdevice/inputdevice.h"
#include "blocks/inputswitch/inputswitch.h"
#include "blocks/joinlr/joinlr.h"
#include "blocks/limiter/limiter.h"
#include "blocks/mixer/mixer.h"
#include "blocks/monoenhancer/monoenhancer.h"
#include "blocks/multibandcompressor/multibandcompressor.h"
#include "blocks/nightlevel/nightlevel.h"
#include "blocks/oneearmix/oneearmix.h"
#include "blocks/outputdevice/outputdevice.h"
#include "blocks/pan/pan.h"
#include "blocks/parametriceq/parametriceq.h"
#include "blocks/peakrmsmeter/peakrmsmeter.h"
#include "blocks/pitchshift/pitchshift.h"
#include "blocks/pluginblock/pluginblock.h"
#include "blocks/recorder/recorder.h"
#include "blocks/reverb/reverb.h"
#include "blocks/simplecompressor/simplecompressor.h"
#ifdef RB_HAVE_MODEL_BLOCK
#include "blocks/modelblock/modelblock.h"
#endif
#ifdef RB_HAVE_RNNOISE
#include "blocks/speechdenoise/speechdenoise.h"
#endif
#include "blocks/soundside/soundside.h"
#include "blocks/spectrummeter/spectrummeter.h"
#include "blocks/speechnow/speechnow.h"
#include "blocks/suddensounds/suddensounds.h"
#include "blocks/sync/sync.h"
#include "blocks/systemaudio/systemaudio.h"
#include "blocks/timeshift/timeshift.h"
#include "blocks/voicecompressor/voicecompressor.h"
#include "blocks/voiceforward/voiceforward.h"
#include "blocks/voicegauge/voicegauge.h"
#include "blocks/volume/volume.h"
#include "blocks/vumeter/vumeter.h"
#include "blocks/wave/wave.h"
#include "blocks/waveformmeter/waveformmeter.h"

#include <string>

namespace rb::blocks
{

bool registerBuiltinBlocks(dsp::BlockRegistry &registry)
{
    bool all = registry.add(std::string{ApplicationSource::kTypeId}, &ApplicationSource::make).has_value()
        && registry.add(std::string{InputDevice::kTypeId}, &InputDevice::make).has_value()
        && registry.add(std::string{OutputDevice::kTypeId}, &OutputDevice::make).has_value()
        && registry.add(std::string{SystemAudio::kTypeId}, &SystemAudio::make).has_value()
        && registry.add(std::string{PeakRmsMeter::kTypeId}, &PeakRmsMeter::make).has_value()
        && registry.add(std::string{AutoLevel::kTypeId}, &AutoLevel::make).has_value()
        && registry.add(std::string{Balance::kTypeId}, &Balance::make).has_value()
        && registry.add(std::string{BassTreble::kTypeId}, &BassTreble::make).has_value()
        && registry.add(std::string{Recorder::kTypeId}, &Recorder::make).has_value()
        && registry.add(std::string{Channels::kTypeId}, &Channels::make).has_value()
        && registry.add(std::string{Declick::kTypeId}, &Declick::make).has_value()
        && registry.add(std::string{Denoise::kTypeId}, &Denoise::make).has_value()
        && registry.add(std::string{Dynamics::kTypeId}, &Dynamics::make).has_value()
        && registry.add(std::string{Dehum::kTypeId}, &Dehum::make).has_value()
        && registry.add(std::string{Echo::kTypeId}, &Echo::make).has_value()
        && registry.add(std::string{PitchShift::kTypeId}, &PitchShift::make).has_value()
        && registry.add(std::string{Reverb::kTypeId}, &Reverb::make).has_value()
        && registry.add(std::string{FirFilter::kTypeId}, &FirFilter::make).has_value()
        && registry.add(std::string{TimeShift::kTypeId}, &TimeShift::make).has_value()
        && registry.add(std::string{MonoEnhancer::kTypeId}, &MonoEnhancer::make).has_value()
        && registry.add(std::string{MultibandCompressor::kTypeId}, &MultibandCompressor::make).has_value()
        && registry.add(std::string{Pan::kTypeId}, &Pan::make).has_value()
        && registry.add(std::string{ParametricEq::kTypeId}, &ParametricEq::make).has_value()
        && registry.add(std::string{Equalizer10::kTypeId}, &Equalizer10::make).has_value()
        && registry.add(std::string{SimpleCompressor::kTypeId}, &SimpleCompressor::make).has_value()
        && registry.add(std::string{VoiceCompressor::kTypeId}, &VoiceCompressor::make).has_value()
        && registry.add(std::string{Limiter::kTypeId}, &Limiter::make).has_value()
        && registry.add(std::string{LowPassFilter::kTypeId}, &LowPassFilter::make).has_value()
        && registry.add(std::string{Sync::kTypeId}, &Sync::make).has_value()
        && registry.add(std::string{HighPassFilter::kTypeId}, &HighPassFilter::make).has_value()
        && registry.add(std::string{Volume::kTypeId}, &Volume::make).has_value()
        && registry.add(std::string{Amplify::kTypeId}, &Amplify::make).has_value()
        && registry.add(std::string{VuMeter::kTypeId}, &VuMeter::make).has_value()
        && registry.add(std::string{SpeechNow::kTypeId}, &SpeechNow::make).has_value()
        && registry.add(std::string{VoiceGauge::kTypeId}, &VoiceGauge::make).has_value()
        && registry.add(std::string{WaveformMeter::kTypeId}, &WaveformMeter::make).has_value()
        && registry.add(std::string{SpectrumMeter::kTypeId}, &SpectrumMeter::make).has_value()
        && registry.add(std::string{SoundSide::kTypeId}, &SoundSide::make).has_value()
        && registry.add(std::string{NightLevel::kTypeId}, &NightLevel::make).has_value()
        && registry.add(std::string{VoiceForward::kTypeId}, &VoiceForward::make).has_value()
        && registry.add(std::string{SuddenSounds::kTypeId}, &SuddenSounds::make).has_value()
        && registry.add(std::string{Ducking::kTypeId}, &Ducking::make).has_value()
        && registry.add(std::string{Mixer::kTypeId}, &Mixer::make).has_value()
        && registry.add(std::string{InputSwitch::kTypeId}, &InputSwitch::make).has_value()
        && registry.add(std::string{JoinLR::kTypeId}, &JoinLR::make).has_value()
        && registry.add(std::string{OneEarMix::kTypeId}, &OneEarMix::make).has_value()
        && registry.add(std::string{FrequencyLowering::kTypeId}, &FrequencyLowering::make).has_value()
        && registry.add(std::string{HearingCompensation::kTypeId16}, &HearingCompensation::make16).has_value()
        && registry.add(std::string{FrequencyRaising::kTypeId}, &FrequencyRaising::make).has_value()
        && registry.add(std::string{HearingCompensation::kTypeId64}, &HearingCompensation::make64).has_value()
        && registry.add(std::string{Wave::kTypeId}, &Wave::make).has_value()
        && registry.add(std::string{PluginBlock::kTypeId}, &PluginBlock::make).has_value()
        && registry.add(std::string{PluginBlock::kLv2TypeId}, &PluginBlock::makeLv2).has_value()
        && registry.add(std::string{PluginBlock::kLadspaTypeId}, &PluginBlock::makeLadspa).has_value();
#ifdef RB_HAVE_CLAP
    // Where the build has the CLAP SDK.
    all = all && registry.add(std::string{PluginBlock::kClapTypeId}, &PluginBlock::makeClap).has_value();
#endif
#ifdef RB_HAVE_VST3
    // Where the build has the VST3 SDK.
    all = all && registry.add(std::string{PluginBlock::kVst3TypeId}, &PluginBlock::makeVst3).has_value();
#endif
#ifdef RB_HAVE_BROADCAST
    // Where the build can keep its password (ADR-0026).
    all = all && registry.add(std::string{Broadcast::kTypeId}, &Broadcast::make).has_value();
#endif
#ifdef RB_HAVE_LIVESTREAM
    // Where the build can draw its picture and keep its key (ADR-0027).
    all = all && registry.add(std::string{LiveStream::kTypeId}, &LiveStream::make).has_value();
#endif
#ifdef RB_HAVE_MODEL_BLOCK
    // Where the build has its host, and ONNX Runtime for it (ADR-0031).
    all = all && registry.add(std::string{ModelBlock::kTypeId}, &ModelBlock::make).has_value();
#endif
#ifdef RB_HAVE_RNNOISE
    // Where the distribution has RNNoise (ADR-0016).
    return all && registry.add(std::string{SpeechDenoise::kTypeId}, &SpeechDenoise::make).has_value();
#else
    return all;
#endif
}

} // namespace rb::blocks

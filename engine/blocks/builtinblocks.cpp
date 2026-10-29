// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/builtinblocks.h"

#include "blocks/applicationsource.h"
#include "blocks/autolevel.h"
#include "blocks/balance.h"
#include "blocks/basstreble.h"
#ifdef RB_HAVE_BROADCAST
#include "blocks/broadcast.h"
#endif
#include "blocks/channels.h"
#ifdef RB_HAVE_LIVESTREAM
#include "blocks/livestream.h"
#endif
#include "blocks/declick.h"
#include "blocks/dehum.h"
#include "blocks/denoise.h"
#include "blocks/ducking.h"
#include "blocks/dynamics.h"
#include "blocks/echo.h"
#include "blocks/equalizer10.h"
#include "blocks/firfilter.h"
#include "blocks/frequencylowering.h"
#include "blocks/frequencyraising.h"
#include "blocks/hearingcompensation.h"
#include "blocks/highpassfilter.h"
#include "blocks/inputdevice.h"
#include "blocks/inputswitch.h"
#include "blocks/joinlr.h"
#include "blocks/limiter.h"
#include "blocks/lowpassfilter.h"
#include "blocks/mixer.h"
#include "blocks/monoenhancer.h"
#include "blocks/multibandcompressor.h"
#include "blocks/outputdevice.h"
#include "blocks/pan.h"
#include "blocks/parametriceq.h"
#include "blocks/peakrmsmeter.h"
#include "blocks/pitchshift.h"
#include "blocks/pluginblock.h"
#include "blocks/recorder.h"
#include "blocks/reverb.h"
#include "blocks/simplecompressor.h"
#ifdef RB_HAVE_RNNOISE
#include "blocks/speechdenoise.h"
#endif
#include "blocks/sync.h"
#include "blocks/systemaudio.h"
#include "blocks/timeshift.h"
#include "blocks/volume.h"
#include "blocks/vumeter.h"
#include "blocks/wave.h"

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
        && registry.add(std::string{Limiter::kTypeId}, &Limiter::make).has_value()
        && registry.add(std::string{LowPassFilter::kTypeId}, &LowPassFilter::make).has_value()
        && registry.add(std::string{Sync::kTypeId}, &Sync::make).has_value()
        && registry.add(std::string{HighPassFilter::kTypeId}, &HighPassFilter::make).has_value()
        && registry.add(std::string{Volume::kTypeId}, &Volume::make).has_value()
        && registry.add(std::string{VuMeter::kTypeId}, &VuMeter::make).has_value()
        && registry.add(std::string{Ducking::kTypeId}, &Ducking::make).has_value()
        && registry.add(std::string{Mixer::kTypeId}, &Mixer::make).has_value()
        && registry.add(std::string{InputSwitch::kTypeId}, &InputSwitch::make).has_value()
        && registry.add(std::string{JoinLR::kTypeId}, &JoinLR::make).has_value()
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
#ifdef RB_HAVE_BROADCAST
    // Where the build can keep its password (ADR-0026).
    all = all && registry.add(std::string{Broadcast::kTypeId}, &Broadcast::make).has_value();
#endif
#ifdef RB_HAVE_LIVESTREAM
    // Where the build can draw its picture and keep its key (ADR-0027).
    all = all && registry.add(std::string{LiveStream::kTypeId}, &LiveStream::make).has_value();
#endif
#ifdef RB_HAVE_RNNOISE
    // Where the distribution has RNNoise (ADR-0016).
    return all && registry.add(std::string{SpeechDenoise::kTypeId}, &SpeechDenoise::make).has_value();
#else
    return all;
#endif
}

} // namespace rb::blocks

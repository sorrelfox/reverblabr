// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/ear.h"

#include "dsp/paramvalue.h"

#include <cmath>
#include <string>

namespace rb::dsp
{

ParamDescriptor earParam()
{
    ParamDescriptor ear;
    ear.id = std::string{kEarParamId};
    ear.type = ParamType::Enum;
    for (const std::string_view choice : kEarChoices) {
        ear.choices.emplace_back(choice);
    }
    ear.defaultValue = ParamValue::ofEnum(std::string{kEarChoices[0]});
    ear.flags.automatable = false;
    ear.flags.inPresets = false;
    return ear;
}

void EarRouter::prepare(double sampleRate, std::uint32_t maxBlockFrames, std::uint32_t latencySamples, Ear ear)
{
    m_maxBlockFrames = maxBlockFrames;
    m_latency = latencySamples;
    m_delay.prepare(kChannels, latencySamples);
    m_delay.setDelaySamples(latencySamples);
    for (std::vector<float> &dry : m_dry) {
        dry.assign(maxBlockFrames, 0.0f);
    }
    m_wet.assign(maxBlockFrames, 0.0f);
    const double fadeSamples = std::max(1.0, sampleRate * static_cast<double>(kFadeMs) / 1000.0);
    m_step = static_cast<float>(1.0 / fadeSamples);
    m_current = ear;
    m_target = ear;
    m_mix = 1.0f;
    m_hold = 0;
}

} // namespace rb::dsp

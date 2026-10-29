// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "dsp/decibel.h"

#include <cmath>

namespace rb::dsp
{

float dbToGain(float db) noexcept RB_NONBLOCKING
{
    return std::pow(10.0f, db / 20.0f);
}

float gainToDb(float gain) noexcept RB_NONBLOCKING
{
    // Written so that NaN also takes the silence branch.
    if (!(gain > kSilenceGain)) {
        return kSilenceDb;
    }
    return 20.0f * std::log10(gain);
}

} // namespace rb::dsp

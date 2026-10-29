// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/frequencyraising.h"

namespace rb::blocks
{

namespace
{

/// The first move 250–500 Hz copied two octaves up and kept, the rest an
/// octave higher or lower each, all unused.
constexpr FrequencyMoveKind kRaising{
    .typeId = FrequencyRaising::kTypeId,
    .methodIds = FrequencyRaising::kMethodIds,
    .defaultMethod = 1,
    .defaultSource = 0,
    .defaults = {{
        {250.0f, 500.0f, 1000.0f, 2000.0f},
        {500.0f, 1000.0f, 2000.0f, 4000.0f},
        {125.0f, 250.0f, 500.0f, 1000.0f},
        {1000.0f, 2000.0f, 3000.0f, 6000.0f},
    }},
    .minimumHz = FrequencyRaising::kMinimumHz,
    .maximumHz = FrequencyRaising::kMaximumHz,
    .raises = true,
    .frameMs = dsp::FrequencyMover::kLowFrameMs,
};

} // namespace

FrequencyRaising::FrequencyRaising()
    : FrequencyMoveBlock(kRaising)
{
}

std::unique_ptr<dsp::Block> FrequencyRaising::make()
{
    return std::make_unique<FrequencyRaising>();
}

} // namespace rb::blocks

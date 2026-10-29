// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/frequencylowering.h"

namespace rb::blocks
{

namespace
{

/// The first move the high consonants' range squeezed into the one below and
/// emptied, the rest an octave lower each, all unused.
constexpr FrequencyMoveKind kLowering{
    .typeId = FrequencyLowering::kTypeId,
    .methodIds = FrequencyLowering::kMethodIds,
    .defaultMethod = 0,
    .defaultSource = 2,
    .defaults = {{
        {4000.0f, 8000.0f, 2500.0f, 4000.0f},
        {2000.0f, 4000.0f, 1250.0f, 2000.0f},
        {6000.0f, 12000.0f, 3000.0f, 6000.0f},
        {1000.0f, 2000.0f, 600.0f, 1000.0f},
    }},
    .minimumHz = FrequencyLowering::kMinimumHz,
    .maximumHz = FrequencyLowering::kMaximumHz,
    .raises = false,
    .frameMs = dsp::FrequencyMover::kFrameMs,
};

} // namespace

FrequencyLowering::FrequencyLowering()
    : FrequencyMoveBlock(kLowering)
{
}

std::unique_ptr<dsp::Block> FrequencyLowering::make()
{
    return std::make_unique<FrequencyLowering>();
}

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "blocks/frequencymoveblock.h"

#include <array>
#include <memory>
#include <string_view>

namespace rb::blocks
{

/// Moves sound out of a high range one ear cannot use into a lower range it
/// hears: the high consonants — s, sh, f, th — that carry plurals and
/// tell "sit" from "fit", for an ear that hears nothing up there however
/// loud it is made.
///
/// Its moves squeeze the source into the destination (Compress) or copy it
/// down by one ratio (Transpose); FrequencyMoveBlock is the rest.
///
/// **Before Hearing Compensation** in a chain, so the moved sound gets the
/// gain of the range it lands in.
///
/// **Latency:** the mover's frame, 1024 samples at 48 kHz, reported.
class FrequencyLowering final : public FrequencyMoveBlock
{
public:
    static constexpr std::string_view kTypeId = "hearing.frequencylowering";
    static constexpr float kMinimumHz = 125.0f;
    static constexpr float kMaximumHz = 16000.0f;
    static constexpr std::array<std::string_view, 2> kMethodIds{"compress", "transpose"};

    FrequencyLowering();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();
};

} // namespace rb::blocks

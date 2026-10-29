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

/// Moves sound out of a low or middle range one ear cannot use into a higher
/// range it hears: for an ear that hears the lows faintly or not at
/// all, or, with Frequency Lowering, for a dip in the middle — the dip's
/// lower half taken down, its upper half up.
///
/// Its moves copy the source up by one ratio (Transpose, the default: moving
/// a low range changes a voice more than moving a high one), squeeze it
/// into the destination (Compress), or give each sound in it its harmonics
/// in the destination (Harmonics, experimental), which keeps a voice's
/// pitch. FrequencyMoveBlock is the rest.
///
/// **Latency:** the frame its lowest move needs, reported: 2048 samples at
/// 48 kHz (about 43 ms) while every move in use starts at kLongFrameBelowHz
/// or above, and 4096 (about 85 ms) once one starts lower, which a low
/// range needs to be told apart. A move crossing that line has the runtime
/// prepare the block again (FrequencyMoveBlock::preparedShape()).
class FrequencyRaising final : public FrequencyMoveBlock
{
public:
    static constexpr std::string_view kTypeId = "hearing.frequencyraising";
    static constexpr float kMinimumHz = 60.0f;
    static constexpr float kMaximumHz = 16000.0f;
    /// A move whose source starts below this takes the long frame.
    static constexpr float kLongFrameBelowHz = 250.0f;
    static constexpr std::array<std::string_view, 3> kMethodIds{"compress", "transpose", "harmonics"};

    FrequencyRaising();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();
};

} // namespace rb::blocks

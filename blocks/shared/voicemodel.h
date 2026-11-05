// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/voicelikelihood.h"

#include <memory>

namespace rb::blocks
{

/// The trained voice detector this build has, for dsp::VoiceLikelihood to
/// run: RNNoise (ADR-0016), of which only the likelihood is kept and the
/// cleaned sound thrown away. Null in a build without it, or when the library
/// was built for another frame. Allocates: call it from prepare().
[[nodiscard]] std::unique_ptr<dsp::VoiceModel> makeVoiceModel();

/// **The word a voice block's detector value carries**: which detector
/// runs, and why the model does not. Speech Now, Night Level and Voice Gauge
/// say it alike, so the interface reads it with one set of words.
struct DetectorReading {
    static constexpr float kModelListening = 0.0f;
    static constexpr float kLevelAndRhythmChosen = 1.0f;
    static constexpr float kNoModelInThisBuild = 2.0f;
    static constexpr float kModelNeedsItsRate = 3.0f;

    /// That word for a model in that state, with that detector preferred.
    [[nodiscard]] static float of(dsp::VoiceModelState model, dsp::VoiceDetector preferred) noexcept RB_NONBLOCKING;
};

} // namespace rb::blocks

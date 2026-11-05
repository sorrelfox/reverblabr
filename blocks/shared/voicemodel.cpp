// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/shared/voicemodel.h"

namespace rb::blocks
{

float DetectorReading::of(dsp::VoiceModelState model, dsp::VoiceDetector preferred) noexcept RB_NONBLOCKING
{
    switch (model) {
    case dsp::VoiceModelState::NotInThisBuild:
        return kNoModelInThisBuild;
    case dsp::VoiceModelState::NeedsModelRate:
        return kModelNeedsItsRate;
    case dsp::VoiceModelState::Ready:
        break;
    }
    return preferred == dsp::VoiceDetector::Model ? kModelListening : kLevelAndRhythmChosen;
}

} // namespace rb::blocks

#ifdef RB_HAVE_RNNOISE

#include <rnnoise.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace rb::blocks
{

namespace
{

/// RNNoise works in the scale of a 16-bit sample, not of a float one.
constexpr float kModelScale = 32768.0f;

/// **RNNoise from the audio thread,** on the terms Speech Denoise has them
/// (ADR-0016): the compiler cannot see into the library, so -Wfunction-effects
/// is quietened for these two calls and nothing else. The rtsan preset and
/// the allocation trap check them at run time, and the state is made whole
/// by rnnoise_create() before the audio thread sees it.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
#endif
[[nodiscard]] float runModel(DenoiseState *state, float *out, const float *in) noexcept RB_NONBLOCKING
{
    return rnnoise_process_frame(state, out, in);
}

void restartModel(DenoiseState *state) noexcept RB_NONBLOCKING
{
    (void)rnnoise_init(state, nullptr);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

class Rnnoise final : public dsp::VoiceModel
{
public:
    explicit Rnnoise(DenoiseState *state) noexcept
        : m_state(state)
    {
    }

    ~Rnnoise() override
    {
        rnnoise_destroy(m_state);
    }

    float hear(std::span<const float, kFrame> frame) noexcept RB_NONBLOCKING override
    {
        std::transform(frame.begin(), frame.end(), m_in.begin(), [](float sample) noexcept RB_NONBLOCKING {
            return sample * kModelScale;
        });
        return runModel(m_state, m_thrownAway.data(), m_in.data());
    }

    void restart() noexcept RB_NONBLOCKING override
    {
        restartModel(m_state);
    }

private:
    DenoiseState *m_state;
    std::array<float, kFrame> m_in{};
    /// The cleaned sound, which the call has to write somewhere.
    std::array<float, kFrame> m_thrownAway{};
};

} // namespace

std::unique_ptr<dsp::VoiceModel> makeVoiceModel()
{
    // The frame the library was built with is the one that gets gathered.
    if (static_cast<std::size_t>(rnnoise_get_frame_size()) != dsp::VoiceModel::kFrame) {
        return nullptr;
    }
    DenoiseState *state = rnnoise_create(nullptr);
    if (state == nullptr) {
        return nullptr;
    }
    return std::make_unique<Rnnoise>(state);
}

} // namespace rb::blocks

#else

namespace rb::blocks
{

std::unique_ptr<dsp::VoiceModel> makeVoiceModel()
{
    return nullptr;
}

} // namespace rb::blocks

#endif

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "model/description.h"
#include "runner.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace rb::modelhost
{

/// **Voice conversion** (ADR-0031): three graphs behind one window of sound.
/// The content encoder hears what is said, the pitch estimator how high, and
/// the voice says it again as itself, at the pitch asked for.
///
/// Each run hears a window and keeps the voice's answer to the hop at its
/// end, faded into what the last run made. Its noise moves on with the sound
/// and its frames fall on one grid, so two runs make nearly the same sound
/// where they overlap.
///
/// A run gives back not-a-number when a graph did, so the path counts it as
/// any other model's answer that is not sound: a run that cannot be trusted
/// is silence, whichever of the three broke it.
class VoiceConverter final : public ModelRunner
{
public:
    VoiceConverter(const std::string &folder, const model::Description &description);
    ~VoiceConverter() override;

    [[nodiscard]] model::Refusal refusal() const noexcept override;
    [[nodiscard]] model::Device device() const noexcept override;
    [[nodiscard]] bool run(std::size_t stream, std::span<const float> input, std::span<float> output) override;
    void clearState() override;
    void setPitchShift(float cents) override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::modelhost

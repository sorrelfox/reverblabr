// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "model/description.h"
#include "model/shared.h"

#include <cstddef>
#include <span>

namespace rb::modelhost
{

/// **What a family comes down to for the path through the host** (ADR-0031):
/// a window of sound in, a hop of sound out. What it takes to get from one to
/// the other is the family's.
class ModelRunner
{
public:
    ModelRunner() = default;
    ModelRunner(const ModelRunner &) = delete;
    ModelRunner &operator=(const ModelRunner &) = delete;
    virtual ~ModelRunner() = default;

    /// Why it will not run; None when it will.
    [[nodiscard]] virtual model::Refusal refusal() const noexcept = 0;
    [[nodiscard]] virtual model::Device device() const noexcept = 0;

    /// One run of \a stream: frameSize samples of each of the model's
    /// channels in \a input, one channel after the other; a hop of each into
    /// \a output, the same way. False when the model broke its promise about
    /// what it gives back.
    [[nodiscard]] virtual bool run(std::size_t stream, std::span<const float> input, std::span<float> output) = 0;

    /// Puts every state back to where a new stream starts.
    virtual void clearState() = 0;

    /// How far a pitch is to move, for a family that has one.
    virtual void setPitchShift(float cents)
    {
        (void)cents;
    }
};

} // namespace rb::modelhost

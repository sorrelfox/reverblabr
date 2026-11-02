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
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rb::modelhost
{

/// **One model, called through ONNX Runtime** (ADR-0031), for one or more
/// independent streams, each with its own state. All of what is called "a
/// family" in the design comes down to a Description, for the families built
/// so far.
class OnnxRunner
{
public:
    /// Loads the model. \a streams is how many runs a frame takes. The
    /// refusal says why it will not run; None when it will.
    OnnxRunner(const std::string &folder, const model::Description &description, std::size_t streams);
    ~OnnxRunner();

    OnnxRunner(const OnnxRunner &) = delete;
    OnnxRunner &operator=(const OnnxRunner &) = delete;

    [[nodiscard]] model::Refusal refusal() const noexcept;
    [[nodiscard]] model::Device device() const noexcept;

    /// One run of \a stream: frameSize samples of each of the model's
    /// channels in \a input, one channel after the other; hopSize of each
    /// into \a output, the same way. False when the model broke its promise
    /// about what it gives back.
    [[nodiscard]] bool run(std::size_t stream, std::span<const float> input, std::span<float> output);

    /// Puts every state back to zeros.
    void clearState();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::modelhost

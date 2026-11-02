// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "model/description.h"
#include "model/shared.h"
#include "runner.h"

#include <onnxruntime_cxx_api.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace rb::modelhost
{

/// **How every graph here is opened**: \a threads of its own and no spinning,
/// and a GPU when the runtime offers one. \a device says what it got. Nothing
/// is ever registered, so a graph holds only what the runtime ships.
[[nodiscard]] Ort::SessionOptions sessionOptions(std::uint32_t threads, model::Device &device);

/// **One model, called through ONNX Runtime** (ADR-0031), for one or more
/// independent streams, each with its own state. The families that are one
/// graph with one sound in and one out come down to a Description.
class OnnxRunner final : public ModelRunner
{
public:
    /// Loads the model. \a streams is how many runs a frame takes. The
    /// refusal says why it will not run; None when it will.
    OnnxRunner(const std::string &folder, const model::Description &description, std::size_t streams);
    ~OnnxRunner() override;

    [[nodiscard]] model::Refusal refusal() const noexcept override;
    [[nodiscard]] model::Device device() const noexcept override;
    [[nodiscard]] bool run(std::size_t stream, std::span<const float> input, std::span<float> output) override;
    void clearState() override;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::modelhost

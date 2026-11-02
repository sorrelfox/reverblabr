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

#include <atomic>
#include <cstdint>

namespace rb::modelhost
{

/// What the host was started with.
struct PipelineOptions {
    std::uint32_t sessionRate = 48000;
    /// The block's latency: a frame later than this is no use to anybody.
    std::uint32_t latency = 0;
    bool downmix = false;
};

/// **Turns the rings' stream of sound into runs of the model and back**, for
/// as long as the process lives (ADR-0031). Returns only if a run throws or
/// the model breaks its promise: that is a crash, and is told as one.
[[nodiscard]] int runPipeline(model::SharedRings &rings,
                              const model::Description &description,
                              const PipelineOptions &options,
                              ModelRunner &runner);

} // namespace rb::modelhost

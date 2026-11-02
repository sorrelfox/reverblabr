// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "model/description.h"
#include "model/shared.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace rb::blocks
{

/// The reverblabr-model-host executable: `REVERBLABR_MODEL_HOST` when it is
/// set, the installed one when it is there, and the one in the build tree
/// otherwise.
[[nodiscard]] std::string defaultModelHostProgram();

/// **What \a folder holds, as the host reads it**: a short process of its
/// own, so a file that breaks the reader breaks nothing else (ADR-0031).
/// Waits for it, five seconds at the most. Off the audio thread.
[[nodiscard]] model::Description inspectFolder(const std::string &program, const std::string &folder);

/// What a host is started for.
struct ModelHostSpec {
    std::string program;
    std::string folder;
    std::uint32_t sampleRate = 48000;
    /// The block's latency: past this behind, a host skips to the present.
    std::uint32_t latency = 0;
    /// A mono model takes the two sides mixed, rather than running twice.
    bool downmix = false;
};

/// **One model host, from the block's side** (ADR-0031): the process, and a
/// thread of its own that starts it and keeps watch. A plain thread, as a
/// block knows nothing of the engine's event loop.
///
/// Whatever happens to the host, the block's audio thread is told only
/// through atomics and never waits for anything here.
class ModelHostLink
{
public:
    /// Starts the watch, which starts the host. \a rings outlives this.
    ModelHostLink(model::SharedRings &rings, ModelHostSpec spec);
    /// Ends the host, at once if it will not go.
    ~ModelHostLink();

    ModelHostLink(const ModelHostLink &) = delete;
    ModelHostLink &operator=(const ModelHostLink &) = delete;

    /// Why the host was given up on: Refusal::None while it is not.
    [[nodiscard]] model::Refusal failure() const noexcept
    {
        return m_failure.load(std::memory_order_relaxed);
    }

    /// Whether a host crashed or hung and another is on its way.
    [[nodiscard]] bool restarting() const noexcept
    {
        return m_restarting.load(std::memory_order_relaxed);
    }

    /// Starts again after a failure, with the count of crashes cleared. Safe
    /// on any thread: it sets one flag.
    void tryAgain() noexcept
    {
        m_tryAgain.store(true, std::memory_order_relaxed);
    }

    /// The running host's process id, or -1: for a test.
    [[nodiscard]] int pid() const noexcept
    {
        return m_pid.load(std::memory_order_relaxed);
    }

private:
    void run(const std::stop_token &stop);
    /// Starts a host; false when it could not even be started.
    [[nodiscard]] bool spawn();
    /// Waits for the running host to be over.
    enum class Ended : std::uint8_t {
        Stopped,
        Refused,
        Crashed
    };
    [[nodiscard]] Ended watch(const std::stop_token &stop);
    void endHost() noexcept;
    /// Reaps the host, and says whether it refused and left its reason.
    [[nodiscard]] bool reap() noexcept;

    model::SharedRings &m_rings;
    ModelHostSpec m_spec;
    int m_child = -1;
    int m_pidfd = -1;
    int m_control = -1;
    std::atomic<int> m_pid{-1};
    std::atomic<model::Refusal> m_failure{model::Refusal::None};
    std::atomic<bool> m_restarting{false};
    std::atomic<bool> m_tryAgain{false};
    std::jthread m_thread;
};

} // namespace rb::blocks

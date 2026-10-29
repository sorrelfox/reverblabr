// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "plugin/shared.h"
#include "rt/realtime.h"

#include <cstdint>
#include <span>

namespace rb::plugin
{

/// **One audio cycle of a plugin host**, with no PipeWire in it (ADR-0014):
/// the heartbeat, the parameter changes waiting in shared memory, and the
/// plugin run over the cycle's buffers.
///
/// Kept apart from the host's node for the reason SessionProcessor is kept
/// apart from the session node: the part that must obey the real-time
/// rules can be run, and checked, with no server anywhere near it.
class HostProcessor
{
public:
    explicit HostProcessor(SharedBlock &shared);

    /// Puts \a next in place, prepared for \a rate, and hands back what was
    /// there. The host calls it on the data thread through a blocking invoke,
    /// between two cycles, so nothing here is freed on the audio thread.
    ///
    /// \a paramCount is how many parameters \a next has, asked on the main
    /// thread: Block::params() is not promised to be real-time safe, so a
    /// cycle never asks it, and a change for a parameter past the end is
    /// dropped rather than handed to the plugin.
    [[nodiscard]] dsp::Block *
    exchange(dsp::Block *next, std::uint32_t rate, std::size_t paramCount) noexcept RB_NONBLOCKING;

    /// One cycle. A null input is silence; a null output is skipped. A cycle
    /// at another rate than the plugin was prepared for comes out silent,
    /// and the host is told through SharedBlock::rateWanted.
    void process(std::span<float *const> inputs,
                 std::span<float *const> outputs,
                 std::uint32_t frames,
                 std::uint32_t rate) noexcept RB_NONBLOCKING;

private:
    SharedBlock &m_shared;
    dsp::Block *m_block = nullptr;
    std::uint32_t m_rate = 0;
    std::size_t m_paramCount = 0;
};

} // namespace rb::plugin

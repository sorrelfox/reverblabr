// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "plugin/hostprocessor.h"

#include "dsp/audioblock.h"

#include <algorithm>
#include <array>

namespace rb::plugin
{

namespace
{

/// The most channels a host passes either way.
constexpr std::size_t kMaxChannels = 8;

void silence(std::span<float *const> outputs, std::uint32_t frames) noexcept RB_NONBLOCKING
{
    for (float *out : outputs) {
        if (out != nullptr) {
            std::fill_n(out, frames, 0.0f);
        }
    }
}

} // namespace

HostProcessor::HostProcessor(SharedBlock &shared)
    : m_shared(shared)
{
}

dsp::Block *
HostProcessor::exchange(dsp::Block *next, std::uint32_t rate, std::size_t paramCount) noexcept RB_NONBLOCKING
{
    dsp::Block *previous = m_block;
    m_block = next;
    m_rate = rate;
    m_paramCount = paramCount;
    m_shared.preparedRate.store(rate, std::memory_order_relaxed);
    m_shared.rateWanted.store(0, std::memory_order_relaxed);
    return previous;
}

void HostProcessor::process(std::span<float *const> inputs,
                            std::span<float *const> outputs,
                            std::uint32_t frames,
                            std::uint32_t rate) noexcept RB_NONBLOCKING
{
    // **The heartbeat first**: a host that got this far this cycle is alive,
    // whatever its plugin does next — and one that never gets here is what
    // the engine's watchdog is watching for.
    m_shared.cycles.fetch_add(1, std::memory_order_release);

    if (m_block == nullptr || frames == 0 || frames > dsp::kMaxBlockFrames) {
        silence(outputs, frames);
        return;
    }
    if (rate != m_rate) {
        m_shared.rateWanted.store(rate, std::memory_order_relaxed);
        silence(outputs, frames);
        return;
    }

    // Every change that arrived before this cycle, heard from its first
    // sample.
    ParamChange change;
    while (popParam(m_shared, change)) {
        if (change.index < m_paramCount) {
            m_block->applyParam(change.index, change.value);
        }
    }

    std::array<float *, kMaxChannels> in{};
    std::array<float *, kMaxChannels> out{};
    const std::size_t inCount = std::min(inputs.size(), kMaxChannels);
    const std::size_t outCount = std::min(outputs.size(), kMaxChannels);
    std::copy_n(inputs.begin(), inCount, in.begin());
    std::copy_n(outputs.begin(), outCount, out.begin());
    dsp::ProcessContext context{dsp::AudioBlock{in.data(), static_cast<std::uint32_t>(inCount), frames},
                                dsp::AudioBlock{out.data(), static_cast<std::uint32_t>(outCount), frames},
                                frames};
    m_block->process(context);
}

} // namespace rb::plugin

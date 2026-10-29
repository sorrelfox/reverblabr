// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/streamtap.h"

#include "rt/audiotap.h"

#include <algorithm>
#include <array>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

} // namespace

void passOnAndTap(ProcessContext &context, rt::AudioTap *tap, std::uint32_t sampleRate) noexcept RB_NONBLOCKING
{
    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    std::array<const float *, kChannels> streamed{};
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        // A channel with no storage is silence, which the tap writes as
        // zeros when it is handed a null pointer.
        streamed[channel] = in.size() >= context.frames ? in.data() : nullptr;
        if (channel >= channels) {
            continue;
        }
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        // **Sample for sample.** Streaming something must not change it.
        if (streamed[channel] != nullptr) {
            std::copy_n(streamed[channel], context.frames, out.data());
        } else {
            std::fill_n(out.data(), context.frames, 0.0f);
        }
    }

    if (tap != nullptr) {
        // Every cycle, pauses and all.
        tap->announceRate(sampleRate);
        (void)tap->write(streamed, context.frames);
    }
}

} // namespace rb::blocks

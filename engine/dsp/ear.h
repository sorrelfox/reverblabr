// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/delayline.h"
#include "dsp/paramdescriptor.h"
#include "rt/realtime.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// Which side of a stereo pair a block works on.
///
/// **One ear is a setting, not a mono lane.** Lanes are stereo pairs and a
/// wire only ever joins ports of the same channel (ADR-0007), so a block for
/// one ear still takes and gives two channels: it works on its own side and
/// passes the other through.
enum class Ear : std::uint8_t {
    Left = 0,
    Right = 1,
};

/// The id every hearing block gives its Ear parameter.
inline constexpr std::string_view kEarParamId = "ear";
/// Its choices, in the order the stored number counts them. Stable for ever:
/// a session file holds them.
inline constexpr std::array<std::string_view, 2> kEarChoices{"left", "right"};

/// The Ear parameter, the same on every block that has one: an enum of
/// kEarChoices, left to start with.
///
/// **Kept out of presets** (ParamFlags::inPresets): a preset made for the
/// left ear has to load into the block for the right one without moving it.
/// **Not automatable**, because changing it is a fade across and not a
/// number gliding; see EarRouter.
[[nodiscard]] ParamDescriptor earParam();

/// The ear a ParamStore number stands for: the position of its choice.
[[nodiscard]] constexpr Ear earFromNumber(float number) noexcept RB_NONBLOCKING
{
    return number >= 0.5f ? Ear::Right : Ear::Left;
}

/// Runs a one-channel process on one side of a stereo signal and passes the
/// other side through, **delayed by the process's own latency**, so the two
/// ears stay sample-aligned.
///
/// A block that works on one ear owns one of these. Its process takes the
/// samples of its ear in place and gives back what that ear hears, late by
/// exactly the latency given to prepare() — the latency the block reports.
/// The other side goes through a delay line of the same length. A fraction
/// of a millisecond between the ears already moves where a sound seems to
/// be, so "about the same" would not do.
///
/// **Changing the ear fades across rather than jumping.** The processed side
/// fades to the unprocessed sound — which the delay line already has, lined
/// up — over kFadeMs; at silence of the difference the ear changes, at a
/// cycle's edge, and the block is told to forget what it heard (the reset
/// passed to process()). The new side is faded in once the process has been
/// fed its own latency again, so its empty delay is never mixed in. At no
/// point does either channel jump.
class EarRouter
{
public:
    /// How long each half of a change of ear takes. Long enough not to
    /// click, short enough that the change is not heard as a fade.
    static constexpr float kFadeMs = 10.0f;

    /// Sizes everything for cycles of up to \a maxBlockFrames and a process
    /// \a latencySamples late, and starts on \a ear with nothing faded.
    /// Allocates; call it from the block's prepare().
    void prepare(double sampleRate, std::uint32_t maxBlockFrames, std::uint32_t latencySamples, Ear ear);

    /// The ear the block is set to. A change takes effect by fading across,
    /// over the next cycles.
    void setEar(Ear ear) noexcept RB_NONBLOCKING
    {
        m_target = ear;
    }

    /// The ear being processed at this moment, which trails setEar() for
    /// the length of a change.
    [[nodiscard]] Ear ear() const noexcept RB_NONBLOCKING
    {
        return m_current;
    }

    /// How much of the processed sound is heard on the processed side, from
    /// 0 (the unprocessed sound alone) to 1.
    [[nodiscard]] float mix() const noexcept RB_NONBLOCKING
    {
        return m_mix;
    }

    /// Forgets everything held: the signal path was cut. A change that was
    /// under way is finished at once, since there is nothing to click. The
    /// block resets its own process as well.
    void reset() noexcept RB_NONBLOCKING
    {
        m_delay.reset();
        m_current = m_target;
        m_mix = 1.0f;
        m_hold = 0;
    }

    /// One cycle. \a processOne(std::span<float>) runs the block's process on
    /// its ear's samples in place; \a resetOne() makes it forget what it has
    /// heard, which happens when the ear changes.
    template <typename ProcessOne, typename ResetOne>
    void process(const ProcessContext &context, ProcessOne &&processOne, ResetOne &&resetOne) noexcept RB_NONBLOCKING
    {
        const std::uint32_t frames = std::min<std::uint32_t>(context.frames, m_maxBlockFrames);
        if (frames == 0) {
            return;
        }
        // **The change happens between cycles**, once the processed side
        // has faded all the way to the unprocessed one: the whole cycle's
        // processing has already been done for the old ear by then.
        if (m_target != m_current && m_mix <= 0.0f) {
            m_current = m_target;
            resetOne();
            m_hold = m_latency;
        }

        // Both inputs are copied before anything is written, so a block
        // whose output is its input's storage reads what arrived.
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            const std::span<float> in = context.input.channel(channel);
            float *const dry = m_dry[channel].data();
            for (std::uint32_t frame = 0; frame < frames; ++frame) {
                dry[frame] = frame < in.size() ? in[frame] : 0.0f;
            }
        }
        const auto processed = static_cast<std::uint32_t>(m_current);
        std::copy_n(m_dry[processed].data(), frames, m_wet.data());
        processOne(std::span<float>{m_wet.data(), frames});
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            m_delay.processChannel(channel, std::span<float>{m_dry[channel].data(), frames});
        }
        m_delay.endFrames(frames);

        const std::span<float> processedOut = context.output.channel(processed);
        const std::span<float> passedOut = context.output.channel(1U - processed);
        const float *const wet = m_wet.data();
        const float *const dryHere = m_dry[processed].data();
        const float *const dryThere = m_dry[1U - processed].data();
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            advanceFade();
            if (frame < processedOut.size()) {
                processedOut[frame] = dryHere[frame] + m_mix * (wet[frame] - dryHere[frame]);
            }
            if (frame < passedOut.size()) {
                passedOut[frame] = dryThere[frame];
            }
        }
    }

private:
    static constexpr std::uint32_t kChannels = 2;

    /// One sample of the fade: down while the ear is changing, then held at
    /// the unprocessed sound until the new side's process has caught up with
    /// its latency, then up.
    void advanceFade() noexcept RB_NONBLOCKING
    {
        if (m_target != m_current) {
            m_mix = std::max(0.0f, m_mix - m_step);
        } else if (m_hold > 0) {
            --m_hold;
        } else if (m_mix < 1.0f) {
            m_mix = std::min(1.0f, m_mix + m_step);
        }
    }

    DelayLine m_delay;
    std::array<std::vector<float>, kChannels> m_dry;
    std::vector<float> m_wet;
    std::uint32_t m_maxBlockFrames = 0;
    std::uint32_t m_latency = 0;
    Ear m_current = Ear::Left;
    Ear m_target = Ear::Left;
    float m_mix = 1.0f;
    float m_step = 1.0f;
    std::uint32_t m_hold = 0;
};

} // namespace rb::dsp

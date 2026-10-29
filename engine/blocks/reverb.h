// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A room around the sound: the sound's reflections, dense and dying away
/// over the decay time, mixed in behind it.
///
/// **A feedback delay network**, our own, from the published designs:
/// - Eight delay lines of different lengths, each a prime number of
///   samples, fed back into each other through a Householder matrix
///   (Jot & Chaigne, "Digital delay networks for designing artificial
///   reverberators", AES 90th Convention, 1991). The matrix loses nothing,
///   so what dies away is set by the lines' own gains alone.
/// - **The decay is exact at low frequencies**: line i, L samples long, is
///   turned down by 10^(-3 L / (fs T60)) on each trip, so every line loses
///   60 dB in T60 seconds whatever its length.
/// - **Damping** puts a one-pole low pass in each line, designed as Jot
///   and Chaigne do so that the decay time at the top of the spectrum is a
///   fraction of the one at the bottom: 0 % keeps them equal, 100 % makes
///   the top die away ten times faster. At long lines and short decays the
///   pole is held below one, so the top dies away a little later than asked.
/// - **Before the network**, the two sides folded to one pass through a
///   pre-delay and four allpasses in series, as the input diffusion of
///   Dattorro's plate ("Effect design, part 1", JAES 45(9), 1997), so the
///   first reflections are already dense rather than eight clean echoes.
/// - **Two outputs** are taken from the lines with sign patterns orthogonal
///   to each other, so the left and the right are as unlike as the network
///   allows; Width folds them towards the middle.
///
/// **As loud whatever the decay.** The input into the network is scaled so
/// that steady noise comes back at about its own level, short decay or long:
/// a longer room spreads the same energy over more time, and Mix alone sets
/// how loud the reverb is against the original.
///
/// **No latency.** The original goes straight through; the pre-delay is part
/// of the effect, like an echo's time, so nothing is lined up with it.
///
/// **Nothing jumps**:
/// - a new size moves every line's length across kSizeGlideMs, read between
///   samples on the way, which bends the reverb's pitch a little while it
///   moves rather than clicking;
/// - the pre-delay glides the same way;
/// - the decay, the damping, the width and the mix glide.
///
/// **Silence stays cheap.** A decaying network ends in numbers too small to
/// be heard that the processor handles slowly (denormals). The session
/// flushes them for its whole cycle; the network also sets anything below
/// kFlushBelow to zero itself, so the block is safe wherever it runs.
class Reverb final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.reverb";

    static constexpr float kLongestPreDelayMs = 250.0f;
    static constexpr float kDefaultPreDelayMs = 20.0f;
    static constexpr float kDefaultSize = 50.0f;
    static constexpr float kShortestDecayMs = 100.0f;
    static constexpr float kLongestDecayMs = 20000.0f;
    static constexpr float kDefaultDecayMs = 2000.0f;
    static constexpr float kDefaultDamping = 40.0f;
    static constexpr float kDefaultWidth = 100.0f;
    static constexpr float kDefaultMix = 25.0f;

    /// How long a new size takes to move the lines.
    static constexpr float kSizeGlideMs = 300.0f;
    /// How long a new pre-delay takes to arrive.
    static constexpr float kPreDelayGlideMs = 100.0f;
    /// How long the decay and the damping take.
    static constexpr float kDecayGlideMs = 50.0f;
    /// How long the width and the mix take.
    static constexpr float kGlideMs = 20.0f;

    /// The network's lines.
    static constexpr std::size_t kLines = 8;
    /// The lines' lengths at the largest size, in milliseconds; the smallest
    /// size is kSmallestScale of these. Spread so that no two are near a
    /// multiple of each other.
    static constexpr std::array<double, kLines> kLineMs{31.3, 37.1, 43.9, 50.3, 57.7, 66.1, 74.9, 83.9};
    static constexpr double kSmallestScale = 0.2;
    /// The input diffusers' lengths in milliseconds, and their gains.
    static constexpr std::array<double, 4> kDiffuserMs{4.771, 3.595, 12.735, 9.307};
    static constexpr std::array<float, 4> kDiffuserGain{0.75f, 0.75f, 0.625f, 0.625f};
    /// The loop filters' coefficients are worked out again every this many
    /// samples while anything they depend on moves: often enough that no
    /// step is heard, counted from the start so the cycle length never
    /// changes what comes out.
    static constexpr std::uint32_t kCoefficientFrames = 32;
    /// Anything the network holds below this is set to zero: about -400 dB,
    /// far under any output format and far above the denormal range.
    static constexpr float kFlushBelow = 1.0e-20f;

    enum Param : std::uint32_t {
        PreDelay = 0, ///< ms before the reverb starts.
        Size, ///< Per cent: the lines' lengths, from a small room to a hall.
        Decay, ///< ms for the reverb to fall by 60 dB (RT60).
        Damping, ///< Per cent: how much faster the top dies away.
        Width, ///< Per cent: 0 is the reverb in the middle, 100 as wide as it comes.
        Mix, ///< Per cent: 0 is the original alone, 100 the reverb alone.
    };

    Reverb();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// The lines' lengths the network is at, or moving to, in samples.
    [[nodiscard]] const std::array<std::uint32_t, kLines> &lineLengths() const noexcept RB_NONBLOCKING
    {
        return m_lengthTo;
    }

private:
    /// A Schroeder allpass: a line whose output is fed back into it and
    /// forward past it, so it smears without colouring.
    struct Diffuser {
        std::vector<float> line;
        std::size_t write = 0;
        float gain = 0.0f;
    };

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    [[nodiscard]] std::array<std::uint32_t, kLines> lengthsFor(float size) const noexcept RB_NONBLOCKING;
    [[nodiscard]] float preDelaySamples() const noexcept RB_NONBLOCKING;
    void settle() noexcept RB_NONBLOCKING;
    void design() noexcept RB_NONBLOCKING;
    [[nodiscard]] float readLine(std::size_t line, float delay) const noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    double m_sampleRate = 48000.0;
    bool m_prepared = false;

    /// The pre-delay's line, of the two sides folded to one.
    std::vector<float> m_preLine;
    std::size_t m_preWrite = 0;
    dsp::LinearSmoother m_preDelay;

    std::array<Diffuser, 4> m_diffusers;

    /// Every line of the network, one after another, each m_capacity long.
    std::vector<float> m_lines;
    std::size_t m_capacity = 1;
    std::size_t m_write = 0;

    /// The lengths moved from and to, and how far the move has gone (0 to 1).
    std::array<std::uint32_t, kLines> m_lengthFrom{};
    std::array<std::uint32_t, kLines> m_lengthTo{};
    dsp::LinearSmoother m_sizeGlide;
    float m_sizeTarget = kDefaultSize;

    /// Each line's loop filter: the gain it passes, the pole, and its state.
    std::array<float, kLines> m_gain{};
    std::array<float, kLines> m_pole{};
    std::array<float, kLines> m_state{};
    float m_inputGain = 1.0f;
    std::uint64_t m_frameCount = 0;

    dsp::LinearSmoother m_decay;
    dsp::LinearSmoother m_damping;
    dsp::LinearSmoother m_width;
    dsp::LinearSmoother m_mix;
};

} // namespace rb::blocks

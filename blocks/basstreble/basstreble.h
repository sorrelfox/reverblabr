// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/biquad.h"
#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A two-band tone control: a lift or a cut for the low end and one
/// for the high end, the quickest way to warm up or brighten a source.
///
/// **Two shelves, each doing only its own end.** Bass is a low shelf at
/// 100 Hz and treble a high shelf at 10 kHz, both at the steepest slope that
/// does not overshoot (a Q of 0.7071), so each reaches its full lift or cut
/// well past its frequency, is half of it at the frequency, and leaves the
/// middle of the range where a voice lives alone.
///
/// **At 0 dB a shelf is exactly nothing.** The state-variable form of a
/// shelf with no gain passes its input straight through, so a Bass & Treble
/// left at its defaults changes the signal by not one bit.
///
/// **Turning a knob does not click.** Each gain glides in decibels over
/// kSmoothingMs, and the shelves are state-variable filters (dsp::Svf),
/// whose coefficients can change every sample, so they are worked out
/// again every sample while a gain moves and not at all once it arrives.
class BassTreble final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.basstreble";

    /// Where each shelf turns over, in hertz.
    static constexpr double kBassFrequency = 100.0;
    static constexpr double kTrebleFrequency = 10000.0;
    /// The shelves' slope: the steepest that does not overshoot.
    static constexpr double kShelfQ = dsp::kButterworthQ;
    /// The most either end can be lifted or cut, in dB.
    static constexpr float kRangeDb = 12.0f;
    /// How long a change of either gain takes to arrive.
    static constexpr float kSmoothingMs = 20.0f;

    enum Param : std::uint32_t {
        Bass = 0, ///< −12 … +12 dB.
        Treble, ///< −12 … +12 dB.
    };

    BassTreble();

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

    /// The gain the block gives a sine at \a frequency with the bass and the
    /// treble at \a bassDb and \a trebleDb, in dB: what the two shelves are
    /// designed to do, worked out rather than measured. What a test holds
    /// the block to, and what a curve on the panel could draw.
    [[nodiscard]] static double
    magnitudeDb(double frequency, double bassDb, double trebleDb, double sampleRate) noexcept;

    /// The two shelves, as a filter each.
    [[nodiscard]] static dsp::FilterSpec bassShelf(double gainDb) noexcept;
    [[nodiscard]] static dsp::FilterSpec trebleShelf(double gainDb) noexcept;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    /// Where both gains are heading, from the store.
    void aimSmoothers() noexcept RB_NONBLOCKING;
    /// Starts both where they are heading, for a start with no glide, and
    /// designs the shelves for them.
    void settleSmoothers() noexcept RB_NONBLOCKING;
    void resetFilters() noexcept RB_NONBLOCKING;

    static constexpr std::uint32_t kChannels = 2;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    double m_sampleRate = 48000.0;
    dsp::LinearSmoother m_bassDb;
    dsp::LinearSmoother m_trebleDb;
    dsp::SvfCoefficients m_bass;
    dsp::SvfCoefficients m_treble;
    std::array<dsp::Svf, kChannels> m_bassFilters{};
    std::array<dsp::Svf, kChannels> m_trebleFilters{};
    bool m_prepared = false;
};

} // namespace rb::blocks

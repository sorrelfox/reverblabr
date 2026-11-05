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

/// What makes one cutoff filter the one it is: the side it keeps, its
/// type id, and the range and the start of its cutoff.
struct CutoffFilterKind {
    std::string_view typeId;
    /// FilterShape::LowPass or FilterShape::HighPass.
    dsp::FilterShape shape = dsp::FilterShape::LowPass;
    float minimumCutoff = 20.0f;
    float maximumCutoff = 20000.0f;
    /// Where a new block starts: the end of the range where it changes
    /// nothing anybody can hear.
    float defaultCutoff = 20000.0f;
};

/// A low-pass or a high-pass filter with a cutoff and a slope: the Low Pass
/// Filter and the High Pass Filter, which differ only in the
/// side they keep and the range of the cutoff.
///
/// **Butterworth, so the pass band is flat.** On the side it keeps the
/// sound is left as it was; at the cutoff it is 3 dB down whichever slope is
/// chosen; past it, it falls at 12 or 24 dB an octave. The 24 dB slope is
/// two sections in a row with the fourth-order Butterworth Qs, not one
/// section twice, which would be 6 dB down at the cutoff and dull on the
/// side it keeps.
///
/// **The cutoff can be dragged while it plays.** The sections are
/// state-variable filters (dsp::Svf), whose coefficients can change every
/// sample, and the cutoff glides there in octaves — an exponential glide of
/// its logarithm — so a sweep sounds even from one end to the other and a
/// jump across the whole range is a quick sweep rather than a thump.
///
/// **Changing the slope does not click either.** Both slopes run all the
/// time, so each has the signal's history when it is wanted, and the output
/// fades from one to the other.
///
/// It starts at the end of its range where it changes nothing anybody can
/// hear: a block dropped into a lane leaves the sound alone until somebody
/// moves the cutoff.
class CutoffFilter : public dsp::Block
{
public:
    /// The stable ids of the slopes.
    static constexpr std::string_view kSlope12 = "12dB";
    static constexpr std::string_view kSlope24 = "24dB";

    /// How long the cutoff takes to get 99 % of the way to a new setting:
    /// long enough to turn a jump into a sweep, short enough to follow a hand.
    static constexpr float kCutoffSmoothingMs = 30.0f;
    /// How long the fade from one slope to the other takes.
    static constexpr float kSlopeFadeMs = 20.0f;

    enum Param : std::uint32_t {
        Cutoff = 0,
        Slope,
    };

    explicit CutoffFilter(const CutoffFilterKind &kind);

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

    /// The gain a filter of \a shape gives a sine at \a frequency with the
    /// cutoff at \a cutoff, in dB: what the sections are designed to do,
    /// worked out rather than measured. What a test holds the block to, and
    /// what a curve on the panel could draw.
    [[nodiscard]] static double
    magnitudeDb(dsp::FilterShape shape, double frequency, double cutoff, bool steep, double sampleRate) noexcept;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors(const CutoffFilterKind &kind);

    /// Works out every section's coefficients for a cutoff, in octaves.
    void design(float octaves) noexcept RB_NONBLOCKING;
    /// Where the cutoff and the slope are heading, from the store.
    void aimSmoothers() noexcept RB_NONBLOCKING;
    /// Starts both where they are heading, for a start with no glide.
    void settleSmoothers() noexcept RB_NONBLOCKING;
    void resetFilters() noexcept RB_NONBLOCKING;

    static constexpr std::uint32_t kChannels = 2;

    dsp::FilterShape m_shape;
    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    double m_sampleRate = 48000.0;
    /// The cutoff in octaves above 1 Hz, gliding.
    dsp::OnePoleSmoother m_octaves;
    /// 0 for the 12 dB slope, 1 for the 24 dB one, and in between while it
    /// fades.
    dsp::LinearSmoother m_steep;

    dsp::SvfCoefficients m_gentle;
    dsp::SvfCoefficients m_steepFirst;
    dsp::SvfCoefficients m_steepSecond;
    std::array<dsp::Svf, kChannels> m_gentleFilters{};
    std::array<dsp::Svf, kChannels> m_steepFirstFilters{};
    std::array<dsp::Svf, kChannels> m_steepSecondFilters{};
    bool m_prepared = false;
};

} // namespace rb::blocks

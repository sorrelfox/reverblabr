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
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// An equaliser whose bands are placed by hand: up to ten, each a
/// bell, a shelf, a pass filter, a notch or a band-pass, at any frequency,
/// width and gain.
///
/// **Ten slots, each in use or not.** A band is six parameters —
/// `bandNUsed`, `bandNOn`, `bandNType`, `bandNFrequency`, `bandNGain`,
/// `bandNQ` for N from 1 to 10 — so a session file is a fixed list whatever
/// has been added and taken away. Adding a band is using a slot nobody is
/// using; deleting one is letting it go, which leaves the others where they
/// are rather than moving every band after it into the slot before.
/// *On* is the band's own switch, for hearing without it and keeping it.
///
/// **Nothing clicks.** Frequency and Q glide in octaves and the gain in
/// decibels, and the sections are state-variable filters (dsp::Svf) that
/// take new coefficients every sample. A band that starts, stops or changes
/// type fades: out to the signal passing it by, over to the new shape, and
/// back in — so a bell becoming a notch is two short fades, not a filter
/// swapped under a signal.
///
/// A new block has three bells at 0 dB, low, middle and high — which is to
/// say it changes nothing until somebody pulls one.
class ParametricEq final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.parametriceq";

    static constexpr std::size_t kBandCount = 10;

    /// The stable ids of the band types, in the order a menu offers them.
    static constexpr std::array<std::string_view, 7>
        kTypeIds{"peak", "lowShelf", "highShelf", "lowPass", "highPass", "notch", "bandPass"};

    static constexpr float kMinimumFrequency = 20.0f;
    static constexpr float kMaximumFrequency = 20000.0f;
    static constexpr float kRangeDb = 18.0f;
    static constexpr float kMinimumQ = 0.1f;
    static constexpr float kMaximumQ = 18.0f;
    static constexpr float kDefaultQ = 1.0f;

    /// How long a frequency or a Q takes to get 99 % of the way to a new
    /// setting, and a gain to arrive.
    static constexpr float kGlideMs = 30.0f;
    static constexpr float kGainSmoothingMs = 20.0f;
    /// How long a band takes to fade in or out.
    static constexpr float kFadeMs = 15.0f;

    /// The parameters of one band, in the order they are listed.
    enum BandParam : std::uint32_t {
        Used = 0,
        On,
        Type,
        Frequency,
        Gain,
        Q,
        BandParamCount,
    };

    /// What one band is set to, as the curve reads it.
    struct Band {
        /// In use and switched on: whether it does anything.
        bool active = false;
        dsp::FilterShape shape = dsp::FilterShape::Peaking;
        double frequency = 1000.0;
        double gainDb = 0.0;
        double q = kDefaultQ;

        [[nodiscard]] bool operator==(const Band &other) const = default;
    };

    ParametricEq();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// The id of one of a band's parameters: `band3Gain`. \a band counts
    /// from nought.
    [[nodiscard]] static std::string paramId(std::size_t band, BandParam which);

    /// The filter shape a type id stands for.
    [[nodiscard]] static dsp::FilterShape shapeOf(std::size_t typeIndex) noexcept;

    /// The response of \a bands at \a frequency, in dB: every active band's
    /// design added up. What a test holds the block to and what the curve
    /// editor draws.
    [[nodiscard]] static double curveDb(double frequency, std::span<const Band> bands, double sampleRate) noexcept;

    /// What every band is set to now. Not for the audio thread.
    [[nodiscard]] std::array<Band, kBandCount> bands() const;

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

private:
    static constexpr std::uint32_t kChannels = 2;

    /// One band on the audio thread: what it is playing now, and how it is
    /// getting to what it has been asked for.
    struct Voice {
        /// The type it is running as, which changes only while it is faded
        /// out.
        std::size_t typeIndex = 0;
        /// Whether it is running at all.
        bool running = false;
        dsp::OnePoleSmoother octaves;
        dsp::LinearSmoother gainDb;
        dsp::OnePoleSmoother logQ;
        /// How much of the band is heard: 0 passes the signal by.
        dsp::LinearSmoother mix;
        dsp::SvfCoefficients coefficients;
        std::array<dsp::Svf, kChannels> filters{};
    };

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    [[nodiscard]] static std::uint32_t index(std::size_t band, BandParam which) noexcept
    {
        return static_cast<std::uint32_t>(band * BandParamCount + which);
    }

    [[nodiscard]] bool wantsRunning(std::size_t band) const noexcept RB_NONBLOCKING;
    [[nodiscard]] std::size_t wantedType(std::size_t band) const noexcept RB_NONBLOCKING;
    void design(Voice &voice) noexcept RB_NONBLOCKING;
    /// Points every voice where the store says, fading where a band starts,
    /// stops or changes type.
    void aimVoices() noexcept RB_NONBLOCKING;
    /// Puts every voice where the store says at once, for a start.
    void settleVoices() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    double m_sampleRate = 48000.0;
    std::array<Voice, kBandCount> m_voices{};
    bool m_prepared = false;
};

} // namespace rb::blocks

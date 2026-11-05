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

/// A graphic equaliser: ten bands an octave apart, each lifted or cut by up
/// to 12 dB.
///
/// **Ten bells at the octave centres**, 31.5 Hz to 16 kHz, each a peaking
/// section with a Q of 1.41 — about an octave wide, so a band touches its
/// neighbours enough for a row of sliders to make a smooth curve, and not so
/// much that one slider moves the whole range. A band at 0 dB is exactly
/// nothing: the state-variable form of a bell with no gain passes its input
/// straight through.
///
/// **A preamp, and a way to keep the level.** The preamp is a plain gain in
/// front of the bands, for making room before a lift. *Keep level* does it
/// automatically: it lowers the output by the highest point of the curve
/// whenever that is above 0 dB, so turning bands up changes the tone without
/// making the loudest part of the sound any louder than it came in.
///
/// **Nothing clicks.** Every gain glides in decibels over kSmoothingMs, and
/// the bands are state-variable filters (dsp::Svf), redesigned every sample
/// while their gain moves and not at all once it has arrived.
class Equalizer10 final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.equalizer10";

    static constexpr std::size_t kBandCount = 10;
    /// Where each band is centred, in hertz: the octave centres.
    static constexpr std::array<double, kBandCount>
        kBandFrequencies{31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
    /// The bands' parameter ids, in the same order. Stable for ever.
    static constexpr std::array<std::string_view, kBandCount> kBandIds{"gain31",
                                                                       "gain63",
                                                                       "gain125",
                                                                       "gain250",
                                                                       "gain500",
                                                                       "gain1k",
                                                                       "gain2k",
                                                                       "gain4k",
                                                                       "gain8k",
                                                                       "gain16k"};
    /// How wide each bell is: about an octave between its half-gain points.
    static constexpr double kBandQ = 1.41;
    /// The most a band, or the preamp, can lift or cut, in dB.
    static constexpr float kRangeDb = 12.0f;
    /// How long a change of any gain takes to arrive.
    static constexpr float kSmoothingMs = 20.0f;

    enum Param : std::uint32_t {
        FirstBand = 0, ///< The ten bands are 0 … 9, low to high.
        Preamp = kBandCount,
        KeepLevel,
    };

    Equalizer10();

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

    /// One band as a filter, at \a gainDb.
    [[nodiscard]] static dsp::FilterSpec band(std::size_t index, double gainDb) noexcept;

    /// The ten bands' curve at \a frequency, in dB: what they are designed to
    /// do, before the preamp and the level keeping. What a test holds the
    /// block to, and what the panel could draw.
    [[nodiscard]] static double
    curveDb(double frequency, const std::array<float, kBandCount> &gainsDb, double sampleRate) noexcept;

    /// How far *Keep level* lowers the output for these gains: the highest
    /// point of the curve if that is above 0 dB, and nothing otherwise. The
    /// curve is looked at each band's centre and half-way between two, where
    /// the top of a row of bells always is. Allocates nothing.
    [[nodiscard]] static float keepLevelDb(const std::array<float, kBandCount> &gainsDb,
                                           double sampleRate) noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    /// The ten gains the store holds.
    [[nodiscard]] std::array<float, kBandCount> storedGains() const noexcept RB_NONBLOCKING;
    /// How much the preamp and the level keeping add up to, from the store.
    [[nodiscard]] float outputGainDb(const std::array<float, kBandCount> &gains) noexcept RB_NONBLOCKING;
    /// Where every gain is heading, from the store.
    void aimSmoothers() noexcept RB_NONBLOCKING;
    /// Starts every gain where it is heading, and designs the bands for it.
    void settleSmoothers() noexcept RB_NONBLOCKING;
    void resetFilters() noexcept RB_NONBLOCKING;

    static constexpr std::uint32_t kChannels = 2;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    double m_sampleRate = 48000.0;
    std::array<dsp::LinearSmoother, kBandCount> m_gainsDb{};
    /// The preamp and the level keeping together, in dB.
    dsp::LinearSmoother m_outputDb;
    std::array<dsp::SvfCoefficients, kBandCount> m_bands{};
    std::array<std::array<dsp::Svf, kBandCount>, kChannels> m_filters{};
    /// The gains the level keeping was last worked out for, so it is worked
    /// out again only when one of them changes.
    std::array<float, kBandCount> m_keptFor{};
    float m_keptDb = 0.0f;
    bool m_prepared = false;
};

} // namespace rb::blocks

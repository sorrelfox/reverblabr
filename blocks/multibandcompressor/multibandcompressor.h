// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audiobuffer.h"
#include "dsp/block.h"
#include "dsp/crossover.h"
#include "dsp/dynamics.h"
#include "dsp/paramstore.h"

#include <array>
#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// The Multiband Compressor: a compressor for each of three bands,
/// for loudness that does not let the bass pump the voice, and for taking
/// the edge off an "s" without dulling everything else.
///
/// **Three bands out of a Linkwitz–Riley crossover** (dsp::ThreeBandCrossover)
/// that add back up to the signal at every frequency, so with the bands left
/// alone the block changes nothing a person can hear; each band then goes
/// through a compressor of its own — **the Dynamics Processor's own**
/// (dsp::DynamicsProcessor), curve and timing, so a band's curve is the curve
/// the Dynamics block would draw for the same numbers. Stereo-linked within
/// each band, like that block, so the image cannot move.
///
/// **No latency**: the splits are filters, not frames, and nothing looks
/// ahead.
class MultibandCompressor final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.multibandcompressor";

    static constexpr std::uint32_t kBands = dsp::kCrossoverBands;

    /// The band's own settings, in the order each band's parameters run.
    enum Field : std::uint32_t {
        Threshold = 0,
        Ratio,
        Attack,
        Release,
        Makeup,
        FieldCount,
    };

    /// The shared parameters, ahead of the bands'.
    enum Shared : std::uint32_t {
        LowCrossover = 0,
        HighCrossover,
        Knee,
        SharedCount,
    };

    /// The index of \a field of \a band (0 low, 1 middle, 2 high).
    [[nodiscard]] static constexpr std::uint32_t bandParam(std::uint32_t band, Field field) noexcept
    {
        return SharedCount + band * FieldCount + field;
    }

    /// The stable id prefix of each band's parameters: "lowThreshold",
    /// "midRatio", "highMakeup".
    static constexpr std::array<std::string_view, kBands> kBandNames{"low", "mid", "high"};

    /// **Which published value is which** (rt::BlockMeter): each band's gain
    /// reduction, in decibels at or below zero, low first. What the tile's
    /// three bars and the panel's meters show.
    static constexpr std::size_t kGainReductionValue = 0;

    /// While the panel asks (rt::BlockMeter::publishBands()), three values a
    /// band, low first: where the band's level is on its curve, how high it
    /// comes out and how much is taken off — the live marker on the band's
    /// curve, as the Dynamics panel draws it.
    static constexpr std::size_t kBandInputValue = 0;
    static constexpr std::size_t kBandOutputValue = 1;
    static constexpr std::size_t kBandReductionValue = 2;
    static constexpr std::size_t kValuesPerBand = 3;

    MultibandCompressor();

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

    /// The curve \a band's controls describe: what its compressor follows,
    /// and what a test puts through dsp::outputDb().
    [[nodiscard]] dsp::DynamicsSettings settings(std::uint32_t band) const noexcept RB_NONBLOCKING;

    /// How \a band's compressor gets there.
    [[nodiscard]] dsp::DynamicsTiming timing(std::uint32_t band) const noexcept RB_NONBLOCKING;

    /// How much \a band's compressor took off in the last cycle, in decibels.
    [[nodiscard]] float gainReduction(std::uint32_t band) const noexcept RB_NONBLOCKING;

    /// The splits as they are this moment, which during a glide is on the way.
    [[nodiscard]] dsp::CrossoverSplits splits() const noexcept RB_NONBLOCKING
    {
        return m_crossover.splits();
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::ThreeBandCrossover m_crossover;
    std::array<dsp::DynamicsProcessor, kBands> m_processors;
    /// The three bands, two channels each, sized in prepare().
    dsp::AudioBuffer m_bandBuffer;
    std::array<std::array<float *, 2>, kBands> m_bandChannels{};
    std::array<float, kBands * kValuesPerBand> m_published{};
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

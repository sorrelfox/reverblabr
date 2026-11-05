// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/ear.h"
#include "dsp/gammatonebank.h"
#include "dsp/hearingbands.h"
#include "dsp/limiter.h"
#include "dsp/paramstore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Hearing Compensation: gain for one ear that depends on frequency
/// and on level. In each band soft sound is lifted a lot and loud sound
/// little, and nothing goes above the band's ceiling (D3).
///
/// - **The filterbank is ADR-0019's**: 64 gammatone channels, 4 ms late,
///   with the bands laid over them.
/// - **Per band:** a level detector at the chosen speed, the gain from the
///   band's soft, average and loud gains, and its ceiling with no attack
///   time (dsp::BandGainControl).
/// - **Then** Loudness over every band, the noise floor under which the lift
///   falls away, the most gain any band may have, and a fade-in when the
///   block starts.
/// - **Last, a true-peak limiter** at a ceiling that can be lowered but not
///   switched off.
/// - **One ear**: the other side passes through, delayed by the same
///   latency.
///
/// **Two blocks, one code**: Hearing Compensation 16 has sixteen
/// bands, each set on its own; Hearing Compensation 64 has one band to each
/// of the filterbank's 64 channels, and is set through up to 24 control
/// points, the bands following smooth curves through them
/// (dsp::bandsFromPoints()), for the steep slopes and narrow dips sixteen
/// bands cannot follow.
class HearingCompensation final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId16 = "hearing.compensation16";
    static constexpr std::size_t kBands16 = 16;
    static constexpr std::string_view kTypeId64 = "hearing.compensation64";
    static constexpr std::size_t kBands64 = 64;
    static constexpr std::size_t kMaxPoints = dsp::kMaxHearingPoints;

    /// How the bands are set: each on its own, or through control points.
    enum class Editing : std::uint8_t {
        Bands,
        Points,
    };

    /// The points a Hearing Compensation 64 starts with, all at 0 dB: one an
    /// octave from 250 Hz to 8 kHz, as an audiogram is laid out. The rest
    /// are off until they are placed.
    static constexpr std::array<double, 6> kDefaultPointsHz{250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0};
    static constexpr double kUnplacedPointHz = 1000.0;

    /// The ranges of the band settings. A gain as low as -30 dB lets a band
    /// be taken down where a recording is too loud for the ear.
    static constexpr float kMinimumGainDb = -30.0f;
    static constexpr float kMaximumGainDb = 60.0f;
    /// Ceilings on the listening scale of the audiogram and the fitting rule
    /// (dsp::kListeningOffsetDb above dBFS): 100 is 15 dB above full scale,
    /// so a ceiling nobody has set holds nothing back.
    static constexpr float kMinimumCeilingDb = 40.0f;
    static constexpr float kMaximumCeilingDb = 140.0f;
    static constexpr float kDefaultCeilingDb = 100.0f;
    /// The final limiter: can be lowered, not switched off.
    static constexpr float kLimiterHighestDb = -1.0f;
    static constexpr float kLimiterLowestDb = -24.0f;

    /// Which of the block's published values is which (rt::BlockMeter): how
    /// far the final limiter took the gain down in the last cycle, and the
    /// mean of the bands' gains now, in dB.
    static constexpr std::size_t kLimiterReductionValue = 0;
    static constexpr std::size_t kMeanGainValue = 1;

    /// The parameters before the bands, in their order.
    enum Param : std::uint32_t {
        Ear = 0,
        Speed,
        Loudness,
        NoiseFloor,
        MaximumGain,
        LimiterCeiling,
        FirstBandParam,
    };
    /// A band's settings, in the order they follow FirstBandParam.
    enum BandParam : std::uint32_t {
        SoftGain = 0,
        AverageGain,
        LoudGain,
        Ceiling,
        BandParamCount,
    };
    /// A control point's settings, in the order they follow FirstBandParam
    /// in a block set through points.
    enum PointParam : std::uint32_t {
        PointOn = 0,
        PointHz,
        PointSoftGain,
        PointAverageGain,
        PointLoudGain,
        PointCeiling,
        PointParamCount,
    };

    /// \a bands bands on the ERB scale, as block type \a typeId, set \a
    /// editing.
    HearingCompensation(std::size_t bands, std::string_view typeId, Editing editing = Editing::Bands);

    [[nodiscard]] static std::unique_ptr<dsp::Block> make16();
    [[nodiscard]] static std::unique_ptr<dsp::Block> make64();

    /// A band setting's id: `band<N><Soft|Average|Loud>GainDb` and
    /// `band<N>CeilingDb`, counting from 0 — the ids the audiogram fit
    /// writes.
    [[nodiscard]] static std::string bandParamId(std::size_t band, BandParam which);

    /// A control point setting's id, counting from 0: `point<N>On`,
    /// `point<N>Hz`, `point<N><Soft|Average|Loud>GainDb` and
    /// `point<N>CeilingDb`.
    [[nodiscard]] static std::string pointParamId(std::size_t point, PointParam which);

    /// The bands' centres for \a bands bands, in hertz.
    [[nodiscard]] static std::vector<double> bandCentresHz(std::size_t bands);

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

    [[nodiscard]] std::size_t bandCount() const noexcept
    {
        return m_bandCount;
    }

    [[nodiscard]] Editing editing() const noexcept
    {
        return m_editing;
    }

    /// Each band's settings as the block last took them, gains in dB and the
    /// ceiling in dBFS of the band's own level: in a block set through
    /// points, what the curves through them came to.
    [[nodiscard]] std::span<const dsp::HearingBand> bandSettings() const noexcept
    {
        return m_bands;
    }

    /// Each band's gain now, in dB, and the level it was taken at, in dBFS:
    /// what the panel's "now" line and band inspector draw, and what the
    /// block publishes as its band values while it is analysed — the gains
    /// first, then the levels.
    [[nodiscard]] std::span<const float> gainsDb() const noexcept
    {
        return m_gainDb;
    }
    [[nodiscard]] std::span<const float> levelsDbfs() const noexcept
    {
        return m_levelDbfs;
    }

    /// How far the final limiter took the gain down in the last cycle, dB.
    [[nodiscard]] float limiterReductionDb() const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] std::vector<dsp::ParamDescriptor> makeDescriptors() const;
    /// Takes the settings off the parameter store, at the top of a cycle.
    void follow() noexcept RB_NONBLOCKING;
    /// One cycle of the ear's own samples, in place.
    void processEar(std::span<float> samples) noexcept RB_NONBLOCKING;
    /// Forgets what the ear's processing has heard.
    void resetEar() noexcept RB_NONBLOCKING;

    std::size_t m_bandCount;
    Editing m_editing;
    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::EarRouter m_router;
    dsp::GammatoneBank m_bank;
    dsp::BandLayout m_layout;
    dsp::BandGainControl m_control;
    dsp::LookaheadLimiter m_limiter;
    dsp::HearingSettings m_settings;
    std::vector<dsp::HearingBand> m_bands;
    /// The control points, in a block set through them.
    std::vector<dsp::HearingPoint> m_points;
    /// The same, spread over the filterbank's channels.
    std::vector<dsp::HearingBand> m_channelBands;
    std::vector<float> m_levelPower;
    std::vector<float> m_channelGainDb;
    /// Each band's gain now and the level it was taken at, read at the
    /// channel nearest its centre.
    std::vector<float> m_gainDb;
    std::vector<float> m_levelDbfs;
    /// What is published while the block is analysed: the gains, then the
    /// levels.
    std::vector<float> m_published;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

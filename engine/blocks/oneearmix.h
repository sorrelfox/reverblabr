// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/loudness.h"
#include "dsp/paramstore.h"
#include "dsp/quadraturefold.h"
#include "dsp/smoothing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Puts both sides of a stereo mix into one ear, or one mix on both sides for
/// a single earbud.
///
/// **Two ways to fold.** One Ear Mix runs each side through
/// dsp::QuadratureFold, so a sound on one side drops 3 dB and sound in
/// antiphase stays. Plain Mono is (L+R)/2, the same fold as Channels' Mono,
/// and is here to compare with and for a mono recording in two channels. Each
/// frame is a crossfade between a side and its filtered self, then a 2×2
/// matrix of weights. No frame of delay.
///
/// **Every change glides, and the glide is in process()**, so no route round
/// it exists: a weight that falls takes kFallMs, and a weight that rises moves
/// at kRiseDbPerSecond, from kRiseFromDb when it starts from nothing. A side
/// that was not in an ear therefore arrives over nearly five seconds, and it
/// does the same at a session's start, when the block is switched on, and
/// when a new output comes within its reach, which the runtime tells it
/// through the momentary parameter NewOutput.
///
/// **It can raise a peak**, by several dB and by 17.2 dB at the very worst,
/// though it adds no power. So it reports GainReach::Adds and a limit belongs
/// after it.
///
/// **Match Levels** is the one measurement here. Asked through the momentary
/// parameter, it measures both folds for a window and publishes both
/// readings. It sets no gain: the panel takes the difference. Outside those
/// seconds no detector runs.
class OneEarMix final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "hearing.oneearmix";

    static constexpr float kFallMs = 10.0f;
    static constexpr float kRiseDbPerSecond = 12.0f;
    static constexpr float kRiseFromDb = -60.0f;
    static constexpr float kMostLoweredDb = 6.0f;
    /// A window of the loudness meter, and a slice over so that its last
    /// block is whole.
    static constexpr float kReadingMs = dsp::GatedLoudness::kWindowMs + dsp::GatedLoudness::kSliceMs;

    /// What the block publishes (rt::BlockMeter). The readings hold from the
    /// end of a reading to the start of the next.
    static constexpr std::size_t kStateValue = 0;
    static constexpr std::size_t kFoldLoudnessValue = 1;
    static constexpr std::size_t kMonoLoudnessValue = 2;
    static constexpr std::size_t kReadingValue = 3;

    /// kStateValue: whether a side that was silent in an ear is coming in.
    static constexpr float kSettled = 0.0f;
    static constexpr float kArriving = 1.0f;
    /// kReadingValue.
    static constexpr float kNotRead = 0.0f;
    static constexpr float kReading = 1.0f;
    static constexpr float kRead = 2.0f;

    enum Param : std::uint32_t {
        SendTo = 0, ///< none, left, right, both.
        Hear, ///< fold, mono.
        LowerDb, ///< What the fold is turned down by.
        MatchLevels, ///< A press: starts a reading.
        NewOutput, ///< A press by the runtime: the arrival starts again.
    };

    /// One weight of the matrix, and the law it moves by.
    class Weight
    {
    public:
        /// Puts \a value in place, settled.
        void reset(float value) noexcept RB_NONBLOCKING;
        /// Brings the weight down to where a rise starts, if it is above it
        /// and heading for sound.
        void restartRise() noexcept RB_NONBLOCKING;
        void setTarget(float target, std::uint32_t fallSamples) noexcept RB_NONBLOCKING;
        /// \a risePerSample is the factor one sample's rise multiplies by.
        [[nodiscard]] float next(double risePerSample) noexcept RB_NONBLOCKING
        {
            if (m_value == m_target) {
                return static_cast<float>(m_value);
            }
            if (m_value > m_target) {
                m_value = m_value - m_fallStep > m_target ? m_value - m_fallStep : m_target;
            } else {
                const double risen = (m_value > kRiseFrom ? m_value : kRiseFrom) * risePerSample;
                m_value = risen < m_target ? risen : m_target;
            }
            if (m_value == m_target) {
                m_fromSilence = false;
            }
            return static_cast<float>(m_value);
        }

        [[nodiscard]] float value() const noexcept RB_NONBLOCKING
        {
            return static_cast<float>(m_value);
        }

        [[nodiscard]] bool settled() const noexcept RB_NONBLOCKING
        {
            return m_value == m_target;
        }

        /// Whether it is on its way up from nothing, and not merely following
        /// a trim.
        [[nodiscard]] bool arriving() const noexcept RB_NONBLOCKING
        {
            return m_fromSilence;
        }

    private:
        static constexpr double kRiseFrom = 0.001;

        double m_value = 0.0;
        double m_target = 0.0;
        double m_fallStep = 0.0;
        bool m_fromSilence = false;
    };

    OneEarMix();

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
    void switchedOn() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// Whether the all-pass chains ran in the last cycle.
    [[nodiscard]] bool filtersRunning() const noexcept RB_NONBLOCKING
    {
        return m_filtersOn;
    }

    /// The four weights as they stand: left from left, left from right, right
    /// from left, right from right.
    [[nodiscard]] std::array<float, 4> weights() const noexcept RB_NONBLOCKING;

private:
    /// What the settings ask for.
    struct Route {
        std::array<float, 4> weights;
        float filtered;
    };

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    [[nodiscard]] Route wantedRoute() const noexcept RB_NONBLOCKING;
    /// Starts from nothing in every weight that carries the far side.
    void start() noexcept RB_NONBLOCKING;
    void startReading() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;

    dsp::QuadratureFold m_fold;
    bool m_filtersOn = false;
    dsp::LinearSmoother m_filtered;
    std::array<Weight, 4> m_weights;
    std::uint32_t m_fallSamples = 1;
    double m_risePerSample = 1.0;

    bool m_readingAsked = false;
    std::uint32_t m_readingLeft = 0;
    std::uint32_t m_readingLength = 1;
    float m_readingStage = kNotRead;
    dsp::GatedLoudness m_foldLoudness;
    dsp::GatedLoudness m_monoLoudness;
    bool m_prepared = false;
};

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/centresplit.h"
#include "dsp/limiter.h"
#include "dsp/loudness.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Splits a stereo sound into what is alike on both sides and the rest, and
/// turns the rest down.
///
/// **The split adds up to the input.** The centre comes from CentreSplit, and
/// the rest is the delayed input less the centre, sample by sample. The output
/// is the delayed input less the part of the rest being taken away, so at no
/// cut it is the delayed input to the bit: the sound never goes through a
/// transform and back, and a change of cut needs no new frame.
///
/// **Two cuts.** One ratio, how much of the voice band is centre, picks between
/// Background and the smaller Between voices, so that the sound falls less
/// while nothing is in the middle. It names no scene and knows no voice: a
/// centred drum holds the larger cut open as a line does. The cut falls at
/// kFallDbPerSecond and rises at kRiseDbPerSecond.
///
/// **Hold level.** What was taken out takes the whole sound down, so a slow
/// gain, at most kMostLiftDb, follows the difference between two gated
/// readings. Nothing can show that taking out the rest cannot raise a sample,
/// and the lift can, so a limiter ends the block.
///
/// **The ceiling goes out of reach at nothing.** The limiter passes a sample
/// untouched only under its ceiling, and a film that peaks over -1 dBFS would
/// be limited by a block set to nothing. So with Background at 0 dB, the cut
/// back and the lift gone, the ceiling rises out of reach, and it comes back
/// before any cut starts: the wait is there so no moved sound meets a ceiling
/// still on its way down.
///
/// **Whatever rises rises at kRiseDbPerSecond or less.** The cut, the lift and
/// the limiter's release take turns, or together they would rise faster.
///
/// **Everything is decided on a hop**, about 5 ms, so nothing depends on the
/// length of a cycle.
class VoiceForward final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.voiceforward";

    static constexpr float kLowestCutDb = -24.0f;
    static constexpr float kDefaultBetweenDb = -3.0f;

    /// How the cut moves.
    static constexpr float kFallDbPerSecond = 60.0f;
    static constexpr float kRiseDbPerSecond = dsp::LookaheadLimiter::kLimitRiseDbPerSecond;

    /// What share of the voice band has to be centre, and how long it holds.
    static constexpr float kCentreFrom = 0.5f;
    static constexpr float kWideUnder = 0.35f;
    static constexpr float kCentreHoldMs = 300.0f;
    static constexpr float kShareSmoothingMs = 100.0f;
    /// A share this high for this long is one sound on both sides: a dry line
    /// on its own must not read so, hence the seconds.
    static constexpr float kAlikeFrom = 0.98f;
    static constexpr float kAlikeSeconds = 5.0f;
    /// Under this the voice band says nothing, in dB of a full-scale tone.
    static constexpr float kQuietDb = -60.0f;

    /// Hold level.
    static constexpr float kMostLiftDb = 6.0f;
    static constexpr float kLiftSmoothingSeconds = 3.0f;
    /// How many 100 ms readings fill the 3 s window: until then, no lift.
    static constexpr std::uint32_t kReadingsToFill = 30;
    /// A lift this small is none, or a smoothing would never arrive.
    static constexpr float kLeastLiftDb = 0.02f;

    /// The ceiling: the plain one, and out of reach.
    static constexpr float kCeilingDb = -1.0f;
    static constexpr float kOutOfReachDb = 40.0f;

    /// What the block publishes (rt::BlockMeter): how much lower the sound is
    /// before any lift, in dB of r.m.s.; what it found, as a Found; the lift in
    /// dB; what the limiter took in dB, 0 or more.
    static constexpr std::size_t kTakenOutValue = 0;
    static constexpr std::size_t kFoundValue = 1;
    static constexpr std::size_t kLiftValue = 2;
    static constexpr std::size_t kLimitValue = 3;

    /// The word for what the mask finds. Whole numbers on kFoundValue.
    enum Found : std::uint8_t {
        Quiet = 0,
        Wide = 1,
        Centre = 2,
        Alike = 3,
    };

    enum Param : std::uint32_t {
        Background = 0, ///< dB of change while there is sound in the centre.
        BetweenVoices, ///< dB of change while there is none.
        HoldLevel, ///< On or off.
    };

    VoiceForward();

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

    /// The cut acting now, in dB, 0 or less.
    [[nodiscard]] float cutDb() const noexcept RB_NONBLOCKING
    {
        return m_cutDb;
    }

    [[nodiscard]] float liftDb() const noexcept RB_NONBLOCKING
    {
        return m_liftDb;
    }

    /// What the limiter took off in the last cycle, in dB: 0 or more.
    [[nodiscard]] float limitedDb() const noexcept RB_NONBLOCKING
    {
        return m_limitedDb;
    }

    /// How much lower the sound is now, before any lift, in dB.
    [[nodiscard]] float takenOutDb() const noexcept RB_NONBLOCKING;

    [[nodiscard]] Found found() const noexcept RB_NONBLOCKING
    {
        return m_found;
    }

    /// The share of the voice band that is centre, smoothed.
    [[nodiscard]] float centreShare() const noexcept RB_NONBLOCKING;

    /// Whether the ceiling is out of reach: the block changes nothing.
    [[nodiscard]] bool neutral() const noexcept RB_NONBLOCKING
    {
        return m_ceilingOpen;
    }

    /// The split, read only, for a measurement that needs the mask.
    [[nodiscard]] const dsp::CentreSplit &split() const noexcept RB_NONBLOCKING
    {
        return m_split;
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    [[nodiscard]] float backgroundDb() const noexcept RB_NONBLOCKING;
    /// What the mask found this hop, the cut wanted and the ceiling.
    void hopEnded() noexcept RB_NONBLOCKING;
    /// A new pair of 100 ms readings: what the lift should be.
    void readingsEnded() noexcept RB_NONBLOCKING;
    /// Runs the limiter over \a count frames from \a start of the output.
    void limit(const dsp::ProcessContext &context, std::uint32_t start, std::uint32_t count) noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::CentreSplit m_split;
    dsp::GatedLoudness m_loudnessIn;
    dsp::GatedLoudness m_loudnessOut;
    dsp::LookaheadLimiter m_limiter;
    rt::BlockMeter *m_meter = nullptr;

    // Per hop and per second, from the rate.
    float m_hopSeconds = 0.0f;
    float m_shareKeep = 0.0f;
    float m_takenKeep = 0.0f;
    float m_liftKeep = 0.0f;
    float m_fallStepDb = 0.0f;
    float m_riseStepDb = 0.0f;
    float m_liftStepDb = 0.0f;
    float m_quietPower = 0.0f;
    std::uint32_t m_ceilingWaitSamples = 0;

    float m_centrePower = 0.0f;
    float m_widePower = 0.0f;
    float m_alikeSeconds = 0.0f;
    float m_holdSeconds = 0.0f;
    bool m_wide = true;
    Found m_found = Quiet;

    float m_wantedCutDb = 0.0f;
    float m_cutDb = 0.0f;
    float m_keep = 1.0f;
    float m_keepAtDb = 0.0f;

    std::uint32_t m_readings = 0;
    float m_liftReading = 0.0f;
    float m_wantedLiftDb = 0.0f;
    float m_liftDb = 0.0f;
    float m_liftGain = 1.0f;
    float m_liftGainAtDb = 0.0f;

    bool m_ceilingOpen = false;
    std::uint32_t m_ceilingWait = 0;
    float m_limitedDb = 0.0f;
    float m_limiterGain = 1.0f;
    bool m_limiterRising = false;

    /// Mean squares of the sound in and of the sound before the lift, over the
    /// hop so far and smoothed over hops.
    float m_inHop = 0.0f;
    float m_outHop = 0.0f;
    float m_inPower = 0.0f;
    float m_outPower = 0.0f;
};

} // namespace rb::blocks

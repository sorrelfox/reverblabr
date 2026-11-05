// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/limiter.h"
#include "dsp/loudness.h"
#include "dsp/paramstore.h"
#include "dsp/voicelikelihood.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Finds the level of speech on a lane, leaves sound near it where it is, and
/// brings louder and quieter sound part of the way towards it, the two amounts
/// set apart.
///
/// **The level comes from what is playing.** Every 100 ms the last 400 ms
/// counts if it sounds and a voice was in most of it. The first 3 s of such
/// speech make the level; after that it follows with a time constant of 10 s
/// counted in speech, by at most 6 LU a step, so one shouted line moves it
/// little. With no voice it stays. After 15 minutes of sound with no voice it
/// is let go, or a lane that went from a film to music would be pressed around
/// an old figure for ever.
///
/// **Until there is a level it does nothing.** A wrong default would move
/// voices, the one thing this block says it leaves alone. So it is the gain at
/// 0 dB and the ceiling at -1 dBFS.
///
/// **A reading for each direction.** Loud sound is read over 400 ms, with the
/// last 100 ms 6 LU down beside it as an early edge. Quiet sound is read over
/// the gated 3 s, which leaves pauses out, or every pause would be lifted, and
/// only when the 400 ms reading is under the band too. Further than kFloorLu
/// under speech nothing is lifted: that is hiss.
///
/// **Slowly up, whatever rises.** The gain falls over 10 ms, and rises over
/// 500 ms at no more than kMostRiseDbPerSecond. The ceiling and the limiter's
/// own release keep to the same rate, and the gain waits while the limiter
/// comes back, or the two together would rise at twice it.
///
/// **The ceiling is tied to the speech level,** because a bang starts before
/// any reading can know of it, and a limiter at -1 dBFS would pass one 20 dB
/// over speech untouched. With both amounts at nought it is out of reach, so
/// that a block set to nothing changes nothing but the delay.
///
/// **It has no Ear:** one gain for both sides, so the image stays.
class NightLevel final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.nightlevel";

    static constexpr float kDefaultTypedLufs = -28.0f;
    static constexpr float kLowestPivotLufs = -45.0f;
    static constexpr float kHighestPivotLufs = -10.0f;

    /// Under this nothing counts as sound (AutoLevel::kGateLufs).
    static constexpr float kGateLufs = -50.0f;
    /// A 400 ms block counts as speech when this share of its frames does.
    static constexpr float kLeastVoicedShare = 0.7f;
    /// How many such blocks make the first reading: 3 s.
    static constexpr std::uint32_t kFirstReadingBlocks = 30;
    /// How long the level follows, in seconds of speech.
    static constexpr float kFollowSeconds = 10.0f;
    /// The most one block can pull it by.
    static constexpr float kMostStepLu = 6.0f;
    /// From here the level is shown as held.
    static constexpr float kHeldSeconds = 30.0f;
    /// After this long of sound with no voice the level goes: 15 minutes.
    static constexpr float kLapseSeconds = 900.0f;

    /// What is left alone, around the speech level.
    static constexpr float kBandAboveLu = 3.0f;
    static constexpr float kBandBelowLu = 5.0f;
    /// Further under the speech level than this nothing is lifted.
    static constexpr float kFloorLu = -30.0f;
    /// The slopes at 100 %: ratios of 2.5:1 and 1.67:1.
    static constexpr float kCutSlope = 0.6f;
    static constexpr float kLiftSlope = 0.4f;
    /// The most the law may ask for either way.
    static constexpr float kMostChangeDb = 24.0f;
    /// How far the 100 ms reading is taken down, so that only sound more than
    /// 9 LU over speech reaches it and syllables stay out.
    static constexpr float kEarlyEdgeLu = 6.0f;

    /// The gain's two time constants.
    static constexpr float kFallMs = 10.0f;
    static constexpr float kRiseMs = 500.0f;
    static constexpr float kMostRiseDbPerSecond = dsp::LookaheadLimiter::kLimitRiseDbPerSecond;

    /// The ceiling: the plain one, where it sits over the speech level at
    /// full cut, what it gains as the cut goes, and out of reach.
    static constexpr float kPlainCeilingDb = -1.0f;
    static constexpr float kCeilingOverSpeechLu = 20.0f;
    static constexpr float kCeilingEaseLu = 20.0f;
    static constexpr float kOutOfReachDb = 40.0f;
    /// How long the state says the ceiling acted, so that a reader taking a
    /// few looks a second sees a short peak too.
    static constexpr float kActingHoldMs = 500.0f;
    /// A limiter acting less than this is not called acting.
    static constexpr float kActingFromDb = 0.1f;

    /// What the block publishes (rt::BlockMeter): the gain applied now in dB,
    /// the limiter's share included; the level found in LUFS, kNoLevel with
    /// none; the 400 ms reading in LUFS; the state, a whole number.
    static constexpr std::size_t kGainValue = 0;
    static constexpr std::size_t kFoundValue = 1;
    static constexpr std::size_t kReadingValue = 2;
    static constexpr std::size_t kStateValue = 3;

    static constexpr float kNoLevel = dsp::kSilentLufs;

    /// Whole units of kStateValue: what the level is, plus 10 while the ceiling
    /// acts, plus 100 times SpeechNow's detector reading, plus 1000 times the
    /// seconds of sound with no voice.
    enum Tracking : std::uint8_t {
        NoSpeechYet = 0,
        Found = 1,
        Typed = 2,
    };
    static constexpr float kPerActing = 10.0f;
    static constexpr float kPerDetector = 100.0f;
    static constexpr float kPerSecond = 1000.0f;

    enum Param : std::uint32_t {
        Cut = 0, ///< Loud sounds, %.
        Boost, ///< Quiet sounds, %.
        PivotFrom, ///< found, typed.
        TypedPivot, ///< LUFS.
    };

    NightLevel();

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

    /// The curve: dB of change for a sound \a readingLufs against a speech
    /// level, with \a cut and \a boost as fractions.
    [[nodiscard]] static float
    lawDb(float readingLufs, float pivotLufs, float cut, float boost) noexcept RB_NONBLOCKING;

    /// Where the ceiling stands, in dBFS. \a pivotLufs is kNoLevel with none.
    [[nodiscard]] static float ceilingDb(float pivotLufs, float cut, float boost) noexcept RB_NONBLOCKING;

    /// The law's gain now, without the limiter.
    [[nodiscard]] float gainDb() const noexcept RB_NONBLOCKING
    {
        return m_gainDb;
    }

    /// What the limiter took off in the last cycle, in dB: 0 or more.
    [[nodiscard]] float limitedDb() const noexcept RB_NONBLOCKING
    {
        return m_limitedDb;
    }

    /// The level found, or kNoLevel.
    [[nodiscard]] float foundLufs() const noexcept RB_NONBLOCKING
    {
        return m_found ? m_foundLufs : kNoLevel;
    }

    /// The level the law goes by now, or kNoLevel.
    [[nodiscard]] float pivotLufs() const noexcept RB_NONBLOCKING;

    /// The last 400 ms.
    [[nodiscard]] float momentaryLufs() const noexcept RB_NONBLOCKING
    {
        return m_loudness.momentary();
    }

    /// The ceiling asked for now, in dBFS.
    [[nodiscard]] float ceilingNowDb() const noexcept RB_NONBLOCKING;

    /// What kStateValue carries now.
    [[nodiscard]] float state() const noexcept RB_NONBLOCKING;

    /// Whether the detector's last frame was voiced.
    [[nodiscard]] bool voicedNow() const noexcept RB_NONBLOCKING
    {
        return m_voice.voiced();
    }

    /// Which detector listens when both can. It has no setting, the block
    /// takes the better one, so this is for a measurement. Call it before a run.
    void preferDetector(dsp::VoiceDetector detector) noexcept RB_NONBLOCKING;

    /// One 400 ms block of the tracker: its level and the share of its frames
    /// that held a voice. process() calls it, and a test hands it blocks no
    /// signal could time this exactly, or fifteen minutes of them.
    void hearBlock(float momentaryLufs, float voicedShare) noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    [[nodiscard]] float wantedGainDb() const noexcept RB_NONBLOCKING;
    void moveCeiling() noexcept RB_NONBLOCKING;
    /// Runs the limiter over \a count frames from \a start of the output.
    void limit(const dsp::ProcessContext &context, std::uint32_t start, std::uint32_t count) noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::GatedLoudness m_loudness;
    dsp::VoiceLikelihood m_voice;
    dsp::LookaheadLimiter m_limiter;
    rt::BlockMeter *m_meter = nullptr;

    /// Which of the last 40 frames were voiced.
    std::array<bool, dsp::GatedLoudness::kSlicesPerBlock> m_voicedFrames{};
    std::size_t m_nextFrame = 0;

    bool m_found = false;
    float m_foundLufs = kNoLevel;
    std::uint32_t m_heard = 0;
    double m_energy = 0.0;
    float m_sinceVoiceSeconds = 0.0f;

    float m_secondsPerSample = 0.0f;
    float m_fallCoefficient = 0.0f;
    float m_wantedDb = 0.0f;
    float m_gainDb = 0.0f;
    float m_limitedDb = 0.0f;
    /// What the limiter's gain was at the last slice's end, and whether it
    /// has risen since the one before.
    float m_limiterGain = 1.0f;
    bool m_limiterRising = false;
    std::uint32_t m_actingSamples = 0;
    std::uint32_t m_actingHoldSamples = 0;
};

} // namespace rb::blocks

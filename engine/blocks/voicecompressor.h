// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/decibel.h"
#include "dsp/limiter.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"
#include "rt/meter.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A voice held steady with three controls: Compression, Gate and Ceiling.
///
/// **One gate, one compressor, one limiter, and everything else decided for
/// you.** Compression is a threshold that also sets the ratio and the make-up,
/// so lowering it takes more off the loud parts and brings everything else up.
/// Below the Gate the sound is gently pushed down, so a room or a breath falls
/// away between words. The Ceiling is where the last limiter stands.
///
/// **The gate comes first**: the compressor reads the level after it, so what
/// the gate took off is not added back by the make-up.
///
/// **The Ceiling is a promise, and also a level.** The output cannot pass it,
/// and the whole sound moves with it. In the top kPushRangeDb of its range
/// the make-up has a share extra, which leans on the limiter: so lowering the
/// Ceiling there means less limiting, and below that range the limiter takes
/// only what the attack lets past.
///
/// **Its release follows the sound.** Two followers chase the same reduction
/// and the deeper one is applied. One is quick both ways, for a syllable. The
/// other is slow both ways, so it only goes down when loud sound stays, and
/// then lets go gently. Nothing depends on the length of a cycle.
///
/// **GainReach::Adds, the default.** The make-up reaches 37.5 dB and the
/// Ceiling is a slider up to full scale, so its limit can be undone with a
/// drag. A last limit after it stays wanted: saying EndsInLimit would let a
/// lowered Hearing limit upstream be passed.
///
/// **It is not neutral when added**: a compressor put in a lane is there to
/// work, as Simple Compressor is. A sound at -18 dBFS comes out about 3.5 dB
/// higher, and peaks stay under -6 dBFS.
class VoiceCompressor final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.voicecompressor";

    static constexpr float kLowestCompressionDb = -36.0f;
    static constexpr float kDefaultCompressionDb = -12.0f;
    static constexpr float kLowestCeilingDb = -36.0f;
    static constexpr float kDefaultCeilingDb = -6.0f;
    /// The Gate at its lowest is off, and exactly off.
    static constexpr float kGateOffDb = -80.0f;

    /// Compression's ratio runs from this at 0 dB to this at its lowest.
    static constexpr float kGentlestRatio = 2.0f;
    static constexpr float kFirmestRatio = 6.0f;
    static constexpr float kKneeDb = 12.0f;
    /// How much extra make-up leans on the limiter with the Ceiling at the top.
    static constexpr float kPushShare = 0.25f;
    /// How far down from the top the Ceiling keeps leaning.
    static constexpr float kPushRangeDb = 12.0f;
    /// Each decibel under the Gate comes out two under, as a gentle expander
    /// does, and never more than this. A gate that shuts takes the room with it
    /// and is heard opening.
    static constexpr float kGateRangeDb = 24.0f;
    static constexpr float kGateKneeDb = 12.0f;

    /// The times are ours, set by reading the sound of them on tone bursts, and
    /// none has a study behind it. The level follows a mean square, as near to
    /// loudness as a follower this quick can be.
    static constexpr float kLevelMs = 8.0f;
    static constexpr float kAttackMs = 5.0f;
    static constexpr float kQuickReleaseMs = 70.0f;
    static constexpr float kSlowAttackMs = 500.0f;
    static constexpr float kSlowReleaseMs = 700.0f;
    static constexpr float kGateOpensMs = 3.0f;
    /// Stays open this long after the level falls, so a level sitting on the
    /// Gate does not open and shut it, and a word's end is kept.
    static constexpr float kGateHoldsMs = 40.0f;
    static constexpr float kGateClosesMs = 120.0f;
    /// A step in Compression moves the threshold, the ratio and the make-up
    /// together. It would be a click, so it glides.
    static constexpr float kCompressionGlideMs = 50.0f;

    /// How long the deepest total stays published. **Longer than the gap
    /// between two meter frames**, or a peak between them is seen by nobody;
    /// the four slots are taken, so the hold is on the reading itself.
    static constexpr float kReadingHoldMs = 120.0f;

    /// What the block publishes (rt::BlockMeter): what the compressor and the
    /// gate took off at the deepest total of the last kReadingHoldMs, what the
    /// limiter took off in the cycle, each as a positive number of dB, and the
    /// level coming in, in dBFS. The first two added are the total the tile
    /// shows.
    static constexpr std::size_t kCompressedValue = 0;
    static constexpr std::size_t kGatedValue = 1;
    static constexpr std::size_t kLimitedValue = 2;
    static constexpr std::size_t kInputValue = 3;

    enum Param : std::uint32_t {
        Compression = 0, ///< dB, lower is more.
        Gate, ///< dB, higher is more; kGateOffDb is off.
        Ceiling, ///< dBFS.
    };

    VoiceCompressor();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// The ratio Compression sets.
    [[nodiscard]] static float ratioAt(float compressionDb) noexcept RB_NONBLOCKING;
    /// What the compressor takes off a steady level, in dB: at or below zero.
    [[nodiscard]] static float compressorDb(float levelDb, float compressionDb) noexcept RB_NONBLOCKING;
    /// What the gate takes off a steady level, in dB: at or below zero.
    [[nodiscard]] static float gateDb(float levelDb, float gateAtDb) noexcept RB_NONBLOCKING;
    /// The make-up those two controls ask for, in dB.
    [[nodiscard]] static float makeupDb(float compressionDb, float ceilingDb) noexcept RB_NONBLOCKING;
    /// **The curve, as one function**: what comes out for a steady level in.
    [[nodiscard]] static float
    outputDb(float levelDb, float compressionDb, float ceilingDb, float gateAtDb) noexcept RB_NONBLOCKING;

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

    /// The ceiling the limiter is going to, in dBFS.
    [[nodiscard]] float ceilingDb() const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    /// One frame's gain, from each side's sample.
    [[nodiscard]] float gainFor(std::span<const float, 2> samples) noexcept RB_NONBLOCKING;
    void publish() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;

    dsp::LookaheadLimiter m_limiter;
    dsp::LinearSmoother m_compression;
    dsp::LinearSmoother m_ceiling;

    float m_levelCoefficient = 0.0f;
    float m_attackCoefficient = 0.0f;
    float m_quickReleaseCoefficient = 0.0f;
    float m_slowAttackCoefficient = 0.0f;
    float m_slowReleaseCoefficient = 0.0f;
    float m_gateOpensCoefficient = 0.0f;
    float m_gateClosesCoefficient = 0.0f;
    std::uint32_t m_gateHoldSamples = 0;

    float m_gateAtDb = kGateOffDb;
    std::array<float, 2> m_meanSquares{};
    float m_levelDb = dsp::kSilenceDb;
    float m_quickDb = 0.0f;
    float m_slowDb = 0.0f;
    float m_gateDb = 0.0f;
    std::uint32_t m_gateHold = 0;
    /// The make-up as Compression and the Ceiling stand, kept while they do.
    float m_makeupDb = 0.0f;

    /// The deepest total held for the meter, and the samples left of it.
    float m_heldCompressedDb = 0.0f;
    float m_heldGatedDb = 0.0f;
    std::uint32_t m_heldFor = 0;
    std::uint32_t m_holdSamples = 0;
};

} // namespace rb::blocks

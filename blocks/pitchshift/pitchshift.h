// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "rt/retirequeue.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

struct PitchEngine;

/// Moves the pitch up or down without the sound running faster or slower:
/// ±12 semitones, and cents on top.
///
/// **SoundTouch does the work** (LGPL-2.1, linked dynamically): it stretches
/// the sound in time by overlapping pieces of it, then reads it back faster or
/// slower, so the length comes out the same and the pitch moves. It is set up
/// as its authors set it up for speech — pieces of 40 ms, a 15 ms search for
/// where two pieces fit, 8 ms of overlap — rather than letting it choose from
/// the tempo, which roughly halves how late the sound is heard.
///
/// **SoundTouch only ever runs in fixed pieces of kChunk frames**, and the
/// sound is taken from what it gave back a fixed latency later. SoundTouch
/// gives its output in bursts of a whole piece at a time, so how much has come
/// back at any moment depends on how the input was cut; reading it a fixed
/// time later, after the longest wait any pitch in the range can cause, makes
/// the output depend on the samples alone, whatever the cycle length. That
/// latency is worked out once per sample rate by running SoundTouch over the
/// whole range (latencyFor()), and reported.
///
/// **A change of pitch is a new SoundTouch.** Changing the pitch of one that
/// is running designs a new anti-aliasing filter, which allocates, so it
/// cannot happen on the audio thread. setParam() builds the new one on the
/// engine thread, already set to the new pitch, runs silence through it until
/// it has grown every buffer it will ever need, and hands it over through one
/// atomic pointer. The audio thread starts feeding it alongside the old one,
/// waits until it has had kSettleFrames of sound to find its feet, crosses
/// over to it across kFadeFrames, and hands the old one back through a retire
/// queue to be freed where freeing is allowed. A change is heard the latency
/// plus about 45 ms later, without a click.
class PitchShift final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.pitchshift";

    static constexpr int kMaxSemitones = 12;
    static constexpr float kMaxCents = 100.0f;

    /// The piece SoundTouch is fed in, whatever the cycle length.
    static constexpr std::uint32_t kChunk = 256;
    /// How long a new SoundTouch runs on the sound before it is heard: its
    /// first output starts from silence and overlaps into the sound.
    static constexpr std::uint32_t kSettleFrames = 1024;
    /// How long the crossover from the old pitch to the new one takes.
    static constexpr std::uint32_t kFadeFrames = 1024;

    /// SoundTouch's settings for speech, which this block keeps whatever the
    /// pitch; see the class comment.
    static constexpr int kSequenceMs = 40;
    static constexpr int kSeekWindowMs = 15;
    static constexpr int kOverlapMs = 8;

    /// What the block publishes (rt::BlockMeter): the shift being heard now,
    /// in semitones, which lags a change by the latency and the crossover.
    static constexpr std::size_t kShiftValue = 0;

    static constexpr std::uint32_t kChannels = 2;

    enum Param : std::uint32_t {
        Semitones = 0, ///< Whole semitones, -12 to 12.
        Cents, ///< Hundredths of a semitone on top, -100 to 100.
    };

    PitchShift();
    ~PitchShift() override;

    PitchShift(const PitchShift &) = delete;
    PitchShift &operator=(const PitchShift &) = delete;

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// How late the sound comes out at \a sampleRate: the longest SoundTouch
    /// keeps anyone waiting, at any pitch in the range, plus one piece.
    /// Worked out by running it, once per rate; not real-time.
    [[nodiscard]] static std::uint32_t latencyFor(double sampleRate);

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

    /// The shift, in semitones, the block is set to: the whole semitones and
    /// the cents together.
    [[nodiscard]] float shift() const noexcept RB_NONBLOCKING;

    /// How many frames were due before SoundTouch had given them back, since
    /// the block was prepared: silence where sound should have been. Zero
    /// whenever the latency is long enough, which is what it is worked out
    /// to be.
    [[nodiscard]] std::uint64_t missedFrames() const noexcept RB_NONBLOCKING
    {
        return m_missed;
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    [[nodiscard]] std::unique_ptr<PitchEngine> makeEngine(float semitones) const;
    /// Frees what the audio thread has handed back. Engine thread only.
    void collect();
    /// Builds an engine for the shift the store holds and hands it over.
    void rebuild();

    void feedChunk() noexcept RB_NONBLOCKING;
    void takePending() noexcept RB_NONBLOCKING;
    [[nodiscard]] float readEngine(const PitchEngine &engine, std::uint32_t channel) noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    double m_sampleRate = 48000.0;
    std::uint32_t m_latency = 0;
    rt::BlockMeter *m_meter = nullptr;
    bool m_prepared = false;

    /// The input piece being gathered, interleaved as SoundTouch wants it.
    std::vector<float> m_chunk;
    std::uint32_t m_fill = 0;
    /// Frames seen since the block was prepared or reset.
    std::uint64_t m_frame = 0;
    std::uint64_t m_missed = 0;

    /// The engine being heard, and the one being crossed over to.
    PitchEngine *m_engine = nullptr;
    PitchEngine *m_incoming = nullptr;
    /// How far the crossover has gone, in frames.
    std::uint32_t m_fadePosition = 0;

    std::atomic<PitchEngine *> m_pending{nullptr};
    rt::RetireQueue m_retired{16};
};

} // namespace rb::blocks

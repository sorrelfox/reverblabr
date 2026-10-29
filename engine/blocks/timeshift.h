// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "blocks/timeshiftspill.h"
#include "dsp/block.h"
#include "dsp/paramstore.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Pauses and rewinds live audio: a recorder for the last few minutes with a
/// play head of its own that everything downstream hears.
///
/// **Everything that comes in is kept**, up to "minutes" of it. Up to
/// kMemoryMinutes that is all in memory. Longer, it is all in a file a
/// thread of the block's own writes behind the audio thread (TimeShiftSpill),
/// and only the last kRecentSeconds are in memory as well. The audio thread
/// only ever copies into and out of memory it was given in prepare(): the
/// spill's thread does every read and write of the file, and hands audio
/// older than memory holds to the audio thread through a ring of its own,
/// ahead of where the play head will want it.
///
/// **The play head.** At live the output is the input, not delayed at all.
/// Paused, the output fades to silence and the head stops while the input
/// goes on being kept, so the head falls behind by exactly the time paused.
/// A jump puts the head exactly the asked-for number of samples behind live
/// and crossfades to it over kFadeMs, from wherever it was; a jump into audio
/// the memory no longer holds waits, playing on, until the spill has read
/// the file there. Jumping forward past live is live.
///
/// **The delay is the point, not a latency.** It is reported through
/// intendedDelaySamples(), which the graph shows and never lines other paths
/// up with.
///
/// The transport is driven through parameters: "paused", and "jump"
/// (a count; each new value moves the head to "target" seconds behind live).
class TimeShift final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.timeshift";

    /// The longest history kept in memory alone; a longer one spills to
    /// disk. Also what is kept when the spill's file cannot be made.
    static constexpr std::int64_t kMemoryMinutes = 10;
    /// How much of a spilled history is in memory as well: what a jump near
    /// live reads without waiting for the disk.
    static constexpr double kRecentSeconds = 60.0;
    /// How long a jump, a pause and a return to live fade across.
    static constexpr float kFadeMs = 10.0f;
    static constexpr std::int64_t kDefaultMinutes = 5;
    static constexpr std::int64_t kLongestMinutes = 60;

    /// What the block publishes (rt::BlockMeter): how far behind live the
    /// head is, and how far back it could go, both in seconds; whether it is
    /// paused; and how many samples the spill could not supply in time.
    static constexpr std::size_t kOffsetValue = 0;
    static constexpr std::size_t kAvailableValue = 1;
    static constexpr std::size_t kPausedValue = 2;
    static constexpr std::size_t kUnderrunValue = 3;

    enum Param : std::uint32_t {
        Paused = 0,
        Target, ///< Seconds behind live that the next jump goes to.
        Jump, ///< A count; each new value is a jump to Target.
        Minutes, ///< How much history is kept, on disk.
    };

    TimeShift();
    ~TimeShift() override;

    TimeShift(const TimeShift &) = delete;
    TimeShift &operator=(const TimeShift &) = delete;

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// Where spill files go: "$XDG_CACHE_HOME/reverblabr/timeshift", or
    /// "~/.cache/reverblabr/timeshift". A test points it somewhere of its
    /// own with setSpillDirectory() before prepare().
    [[nodiscard]] static std::filesystem::path defaultSpillDirectory();
    void setSpillDirectory(std::filesystem::path directory);
    /// Whether the spill works on a thread of its own (the default) or
    /// only when a test steps it; before prepare().
    void setSpillMode(TimeShiftSpill::Mode mode);
    /// The spill, while the history is longer than memory holds and its
    /// file could be made; null otherwise.
    [[nodiscard]] TimeShiftSpill *spill() noexcept
    {
        return m_spill.get();
    }

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
    [[nodiscard]] std::uint32_t intendedDelaySamples() const noexcept RB_NONBLOCKING override;

    /// How far behind live the head is, in samples: 0 at live.
    [[nodiscard]] std::uint64_t offsetSamples() const noexcept RB_NONBLOCKING;
    /// How far behind live the head could go now, in samples.
    [[nodiscard]] std::uint64_t availableSamples() const noexcept RB_NONBLOCKING;
    /// Whether a jump is waiting for the spill to read the file.
    [[nodiscard]] bool jumpPending() const noexcept RB_NONBLOCKING
    {
        return m_pending;
    }
    /// Samples the spill did not have ready when the head needed them.
    [[nodiscard]] std::uint64_t underruns() const noexcept RB_NONBLOCKING
    {
        return m_underruns;
    }

private:
    static constexpr std::uint32_t kChannels = 2;

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// The frame at absolute position \a position, from memory or from the
    /// spill's ring; false if neither has it.
    [[nodiscard]] bool fetch(std::uint64_t position, float (&frame)[kChannels]) noexcept RB_NONBLOCKING;
    /// Makes the jump waiting, if what it needs is there.
    void tryJump() noexcept RB_NONBLOCKING;
    /// Where the jump waiting would put the head, now.
    [[nodiscard]] std::uint64_t pendingTarget() const noexcept RB_NONBLOCKING;
    /// Whether \a position is older than memory holds, now.
    [[nodiscard]] bool isFar(std::uint64_t position) const noexcept RB_NONBLOCKING
    {
        return m_written - std::min(position, m_written) > m_ramFrames;
    }

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;
    std::filesystem::path m_spillDirectory;
    TimeShiftSpill::Mode m_spillMode = TimeShiftSpill::Mode::Threaded;
    double m_sampleRate = 48000.0;

    /// The most recent input, interleaved, and how many frames have
    /// ever been written.
    std::vector<float> m_ram;
    std::uint64_t m_ramFrames = 1;
    std::uint64_t m_written = 0;
    /// The furthest behind live the head may be, in frames.
    std::uint64_t m_historyFrames = 0;
    std::unique_ptr<TimeShiftSpill> m_spill;

    /// The play head, and the one it is fading from.
    std::uint64_t m_head = 0;
    std::uint64_t m_fadingFrom = 0;
    std::uint32_t m_fadeLeft = 0;
    std::uint32_t m_fadeLength = 1;
    /// The pause's fade, in steps: m_fadeLength playing, 0 paused, so it
    /// lands exactly on both. A jump the spill's ring cannot crossfade dips
    /// through silence the same way.
    std::uint32_t m_level = 0;
    bool m_dip = false;

    /// A jump asked for and not yet made: this many samples behind live.
    std::uint64_t m_pendingOffset = 0;
    bool m_pending = false;
    float m_seenJump = 0.0f;
    std::uint64_t m_underruns = 0;
    bool m_prepared = false;
};

} // namespace rb::blocks

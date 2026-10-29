// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "rt/audiotap.h"
#include "rt/realtime.h"
#include "rt/seqlock.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace rb::rt
{

/// What one channel measured since somebody last looked.
///
/// **Since last read, not "at the moment".** A meter that published the
/// latest value per cycle would lose a click at a small quantum and stutter
/// at a large one: at 2048 frames a cycle happens 47 times a second, and a
/// single sample of it decides nothing. An accumulator that the reader
/// empties shows every peak whatever the quantum, and the reader can run at
/// whatever rate the screen does.
struct MeterReading {
    /// The largest sample seen, as an amplitude. Zero when nothing was heard
    /// — **not** silence in decibels, because there is no such number.
    float peak = 0.0f;
    /// The root mean square of everything seen, as an amplitude.
    float rms = 0.0f;
    /// How many frames it was measured over. Zero means nothing has run.
    std::uint32_t frames = 0;
    /// Whether a sample reached or passed full scale since the last read.
    bool clipped = false;

    /// Whether there was a signal worth calling a signal: about −80 dBFS,
    /// which is what the wires and the state dots go by.
    [[nodiscard]] bool isActive() const noexcept
    {
        return peak >= kActivity;
    }

    /// −80 dBFS as an amplitude.
    static constexpr float kActivity = 0.0001f;
};

/// One output channel's accumulator: written on the audio thread, emptied by
/// whoever is sampling.
///
/// **Lock-free and allocation-free**, and it never holds the audio thread up:
/// a cycle costs one compare-and-swap for the peak and three ordinary atomic
/// operations, however many frames it carried. The arithmetic itself is done
/// in registers over the block and only the result is published.
///
/// Everything is relaxed. There is nothing else for these numbers to be
/// ordered against: a reader that sees a peak from this cycle and a frame
/// count from the last one is a reader that is a few hundred microseconds out
/// on a meter, which is not something a person can see.
class ChannelMeter
{
public:
    ChannelMeter() = default;
    ChannelMeter(const ChannelMeter &) = delete;
    ChannelMeter &operator=(const ChannelMeter &) = delete;

    /// Measures one cycle of one channel. Real-time.
    void measure(std::span<const float> samples) noexcept RB_NONBLOCKING
    {
        float peak = 0.0f;
        float sum = 0.0f;
        for (const float sample : samples) {
            const float magnitude = std::fabs(sample);
            peak = magnitude > peak ? magnitude : peak;
            sum += sample * sample;
        }
        // **A NaN never reaches the peak**: it compares false against
        // everything, so a block that has blown up would read as perfectly
        // quiet. The sum is where it shows, because adding to it carries it
        // along — as does an infinity from a runaway feedback path. Either
        // one is shown as full scale, with the clip latched, because a meter
        // pinned at the top is how a person finds out.
        if (!std::isfinite(sum)) {
            peak = 1.0f;
            sum = static_cast<float>(samples.size());
        }
        add(peak, sum, static_cast<std::uint32_t>(samples.size()));
    }

    /// Adds a block's worth of figures without looking at samples, for a
    /// caller that has measured them already. Real-time.
    ///
    /// **Into whichever of two sets of totals the reader is not taking**.
    /// A cycle's peak, sum and frame count go together, and three
    /// atomics taken one after another could split a cycle between two
    /// readings: its frames in one with no peak — a block measured, and
    /// silent — and its peak in the next with no frames, thrown away as
    /// nothing measured. So the writer says it is busy, adds to the set it
    /// is pointed at, and says it has finished; the reader points it at the
    /// other set first, and waits out a write that may still be finishing
    /// in the one it is taking. Two uncontended increments a cycle here; the
    /// waiting is the reader's.
    void add(float peak, float sumOfSquares, std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        m_busy.fetch_add(1, std::memory_order_seq_cst);
        Totals &totals = m_totals[m_writing.load(std::memory_order_seq_cst)];
        totals.peak = peak > totals.peak ? peak : totals.peak;
        totals.sumOfSquares += sumOfSquares;
        totals.frames += frames;
        totals.clipped = totals.clipped || peak >= 1.0f;
        m_busy.fetch_add(1, std::memory_order_release);
    }

    /// Takes everything measured since the last time and starts again. For
    /// the sampler; the audio thread never calls it. It may wait for as long
    /// as one add() takes, which is a few instructions.
    [[nodiscard]] MeterReading take() noexcept
    {
        // Only this side ever moves the writer, so what it was pointed at is
        // known without asking anybody.
        const std::uint32_t taking = m_taking;
        m_taking = 1 - taking;
        m_writing.store(m_taking, std::memory_order_seq_cst);
        // A write that started before the switch may still be adding to the
        // set about to be read. It is done once the count has moved on from
        // an odd number — the next write, if one has started, is into the
        // other set.
        const std::uint32_t busy = m_busy.load(std::memory_order_seq_cst);
        if ((busy & 1U) != 0) {
            while (m_busy.load(std::memory_order_acquire) == busy) { }
        }

        Totals &totals = m_totals[taking];
        MeterReading reading;
        reading.peak = totals.peak;
        reading.frames = totals.frames;
        reading.clipped = totals.clipped;
        reading.rms = totals.frames == 0 ? 0.0f : std::sqrt(totals.sumOfSquares / static_cast<float>(totals.frames));
        totals = Totals{};
        return reading;
    }

    /// Empties it without reading, for a block that has just been connected
    /// and should not show what it measured before.
    void clear() noexcept
    {
        (void)take();
    }

private:
    struct Totals {
        float peak = 0.0f;
        float sumOfSquares = 0.0f;
        std::uint32_t frames = 0;
        bool clipped = false;
    };

    std::array<Totals, 2> m_totals{};
    /// Which set the writer adds to. Written only by the reader.
    std::atomic<std::uint32_t> m_writing{0};
    /// The reader's own copy of the same.
    std::uint32_t m_taking = 0;
    /// Odd while the writer is inside add().
    std::atomic<std::uint32_t> m_busy{0};

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "a meter must never take a lock");
};

inline constexpr std::size_t kMaxMeterChannels = 8;
/// How many numbers of its own a block may publish alongside its levels —
/// a gain reduction, an input level, whatever the block's face shows.
inline constexpr std::size_t kMaxMeterValues = 4;
/// How many band values an analysed block may publish for its panel's
/// graph: two a band for the 64 bands of the widest hearing block,
/// its gain now and the level that gain was taken at.
inline constexpr std::size_t kMaxBandValues = 128;

/// **One cycle's band values**, as a block published them: what it is doing
/// in each of its bands now, such as the gain it applies. What a value means
/// is the block's business and its panel's, as with the meter values.
struct BandValues {
    std::uint32_t count = 0;
    float values[kMaxBandValues]{};
};

/// Everything measured about one block: a meter per output channel, and a
/// few numbers the block itself publishes.
///
/// One of these belongs to a block for as long as the block is in the
/// session, and **not** to a compiled graph: a recompile builds a new plan
/// around the same blocks, and a meter that was thrown away with the plan
/// would blink every time anything was edited.
class BlockMeter
{
public:
    BlockMeter() = default;
    BlockMeter(const BlockMeter &) = delete;
    BlockMeter &operator=(const BlockMeter &) = delete;

    [[nodiscard]] ChannelMeter &channel(std::size_t index) noexcept RB_NONBLOCKING
    {
        return m_channels[index < kMaxMeterChannels ? index : kMaxMeterChannels - 1];
    }

    /// Publishes one of the block's own numbers, such as a gain reduction.
    /// Real-time: one atomic store. What each index means is the block's
    /// business, and its face's.
    void publish(std::size_t index, float value) noexcept RB_NONBLOCKING
    {
        if (index < kMaxMeterValues) {
            m_values[index].store(value, std::memory_order_relaxed);
        }
    }

    [[nodiscard]] float value(std::size_t index) const noexcept
    {
        return index < kMaxMeterValues ? m_values[index].load(std::memory_order_relaxed) : 0.0f;
    }

    /// How many channels this block is being metered on, which the runtime
    /// sets when it prepares the block.
    void setChannelCount(std::size_t count) noexcept
    {
        m_channelCount.store(count > kMaxMeterChannels ? kMaxMeterChannels : count, std::memory_order_relaxed);
    }

    [[nodiscard]] std::size_t channelCount() const noexcept RB_NONBLOCKING
    {
        return m_channelCount.load(std::memory_order_relaxed);
    }

    /// **Starts copying the block's output and input out for their spectra**,
    /// and taking the band values it publishes, or stops.
    /// Engine thread only.
    ///
    /// The rings are made the first time and kept for as long as the meter
    /// is, which is as long as the block is in the session: the audio thread
    /// reads the flag and then writes into a ring, and a ring freed while
    /// switched off could be one it is half-way through writing to. Switched
    /// off, the audio thread does nothing but read the flag.
    void setAnalysing(bool on)
    {
        if (on && !m_analysis) {
            m_analysis = std::make_unique<AudioTap>(kAnalysisChannels, kAnalysisFrames);
            m_inputAnalysis = std::make_unique<AudioTap>(kAnalysisChannels, kAnalysisFrames);
        }
        m_analysing.store(on && m_analysis != nullptr, std::memory_order_release);
    }

    [[nodiscard]] bool isAnalysing() const noexcept RB_NONBLOCKING
    {
        return m_analysing.load(std::memory_order_acquire);
    }

    /// Copies one cycle of the block's output into the ring, if somebody is
    /// looking at its spectrum. Real-time: one flag read when nobody is, and
    /// a copy when somebody is. A full ring drops the cycle, as any tap does.
    void analyse(std::span<const float *const> outputs, std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        if (m_analysing.load(std::memory_order_acquire)) {
            (void)m_analysis->write(outputs, frames);
        }
    }

    /// **The same for what goes into the block**, copied before the
    /// block runs, so that a panel can draw the sound before it beside the
    /// sound after it. A block with no inputs has nothing copied.
    void analyseInput(std::span<const float *const> inputs, std::uint32_t frames) noexcept RB_NONBLOCKING
    {
        if (m_analysing.load(std::memory_order_acquire)) {
            (void)m_inputAnalysis->write(inputs, frames);
        }
    }

    /// The ring the engine thread drains, or null before the first
    /// setAnalysing(true). Engine thread only.
    [[nodiscard]] AudioTap *analysis() const noexcept
    {
        return m_analysis.get();
    }

    /// The input side's ring, made and kept with the output's.
    [[nodiscard]] AudioTap *inputAnalysis() const noexcept
    {
        return m_inputAnalysis.get();
    }

    /// **Publishes the block's band values for its panel**: up to
    /// kMaxBandValues, the rest dropped. Real-time. **Only while somebody is
    /// looking**: one flag read otherwise, so a block may call it every
    /// cycle, and one that has work to do for them can ask isAnalysing()
    /// first and skip it.
    ///
    /// Through a seqlock rather than an atomic a band, so that the reader
    /// gets one cycle's bands together, never half of one cycle and half of
    /// the next: a graph drawn from two cycles has a step in it that the
    /// sound never had. The audio thread is the only writer.
    void publishBands(std::span<const float> values) noexcept RB_NONBLOCKING
    {
        if (!m_analysing.load(std::memory_order_acquire)) {
            return;
        }
        BandValues bands;
        bands.count = static_cast<std::uint32_t>(values.size() < kMaxBandValues ? values.size() : kMaxBandValues);
        for (std::uint32_t band = 0; band < bands.count; ++band) {
            bands.values[band] = values[band];
        }
        m_bands.store(bands);
    }

    /// The band values last published, for the sampler.
    [[nodiscard]] BandValues bands() const noexcept
    {
        return m_bands.load();
    }

    /// How many times band values have been published, so that the sampler
    /// can tell a block that has published since it started looking from
    /// one whose values are left over from an earlier look.
    [[nodiscard]] std::uint64_t bandPublishCount() const noexcept
    {
        return m_bands.publishCount();
    }

    /// How many channels a spectrum is taken from (they are mixed to one),
    /// and how long its ring is: a quarter of a second at 192 kHz, far more
    /// than the engine thread lets pile up between two looks.
    static constexpr std::uint32_t kAnalysisChannels = 2;
    static constexpr std::size_t kAnalysisFrames = 1 << 16;

    /// Forgets everything, for a block that has just been connected or
    /// switched off.
    void clear() noexcept
    {
        for (ChannelMeter &meter : m_channels) {
            meter.clear();
        }
        for (std::atomic<float> &value : m_values) {
            value.store(0.0f, std::memory_order_relaxed);
        }
    }

private:
    std::array<ChannelMeter, kMaxMeterChannels> m_channels;
    std::array<std::atomic<float>, kMaxMeterValues> m_values{};
    std::atomic<std::size_t> m_channelCount{0};
    std::unique_ptr<AudioTap> m_analysis;
    std::unique_ptr<AudioTap> m_inputAnalysis;
    SeqlockValue<BandValues> m_bands;
    std::atomic<bool> m_analysing{false};
};

} // namespace rb::rt

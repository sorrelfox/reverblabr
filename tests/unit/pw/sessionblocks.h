// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/blockinfo.h"
#include "rt/meter.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string_view>

namespace rb::test
{

/// A source block that passes on whatever arrives at it.
///
/// This is what a session node's input lane needs: a block of the source
/// category — the compiler insists a graph has one — whose input ports are
/// the ones PipeWire's buffers are bound to. The real capture block
/// will do the channel mapping as well; this one only carries the signal, so
/// that a test measuring a level is measuring the path and not the block.
class PassThroughSource final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "source.passThrough";

    explicit PassThroughSource(std::uint32_t channels = 2);

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return {};
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

    /// How many cycles it has run, so a test can tell that it did.
    [[nodiscard]] std::uint64_t cycleCount() const noexcept
    {
        return m_cycles;
    }

    /// The largest sample it has seen since the count was last read, which
    /// is enough to tell audio from silence without a meter.
    [[nodiscard]] float peak() const noexcept
    {
        return m_peak;
    }

    void clearPeak() noexcept
    {
        m_peak = 0.0f;
    }

private:
    dsp::BlockInfo m_info;
    std::uint64_t m_cycles = 0;
    float m_peak = 0.0f;
};

/// An effect that multiplies what arrives by a fixed gain.
///
/// It is what a test inserts into a running lane: the sound it makes is
/// plainly different, so whether the change was faded in can be measured,
/// and it keeps no state, so nothing about it can survive a recompile by
/// accident.
class FixedGainEffect final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.fixedGain";

    explicit FixedGainEffect(float gain = 1.0f, std::uint32_t channels = 2);

    /// Half the signal, which is plainly audible against unity.
    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return {};
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

    [[nodiscard]] float gain() const noexcept
    {
        return m_gain;
    }

    /// How many cycles it has run, so a test can tell it is in the graph.
    [[nodiscard]] std::uint64_t cycleCount() const noexcept
    {
        return m_cycles;
    }

private:
    dsp::BlockInfo m_info;
    float m_gain = 1.0f;
    std::uint64_t m_cycles = 0;
};

/// **Half the signal, and 64 band values every cycle**: what a
/// hearing block's panel draws one of a band. Value `i` is bandValue(i), so
/// a test knows each one and where it landed.
class BandWriterEffect final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.bandWriter";

    BandWriterEffect();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    /// What band \a band says: distinct for every band, and not a number a
    /// band left at zero could be mistaken for.
    [[nodiscard]] static constexpr float bandValue(std::size_t band) noexcept
    {
        return -30.0f + 0.5f * static_cast<float>(band);
    }

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return {};
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override { }
    void applyParam(std::uint32_t, float) noexcept RB_NONBLOCKING override { }

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view, const dsp::ParamValue &) override
    {
        return std::unexpected(dsp::BlockError::UnknownParam);
    }
    [[nodiscard]] dsp::ParamValue param(std::string_view) const override
    {
        return {};
    }
    [[nodiscard]] dsp::ParamValueMap saveState() const override
    {
        return {};
    }
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &) override
    {
        return {};
    }

private:
    dsp::BlockInfo m_info;
    rt::BlockMeter *m_meter = nullptr;
    std::array<float, rt::kMaxBandValues> m_bands{};
};

/// What a \ref LifetimeWatchedEffect writes down about itself.
///
/// **Whether a block outlived the graph that was running it cannot be asked
/// after the fact**: by then the answer is either yes, or the process has
/// already faulted. So the block counts the cycles it runs and latches that
/// count as it is destroyed. A test that read the count before it asked for
/// the block to be removed can then tell the two cases apart — a block freed
/// where the edit first noticed it was gone has the count it had then, and a
/// block freed after the graph was swapped has a higher one, because the
/// node fades and swaps before the edit returns and the block goes on being
/// processed throughout.
struct LifetimeWatch {
    /// Cycles the block has run.
    std::atomic<std::uint64_t> cycles{0};
    /// What `cycles` said when the block was destroyed.
    std::atomic<std::uint64_t> cyclesWhenDestroyed{0};
    /// How many of them have been destroyed, so a test can tell a block that
    /// was kept alive from one that was leaked.
    std::atomic<int> destructions{0};
    /// How many times one of them has been prepared.
    ///
    /// **Preparing is not free and it is not harmless**: it may
    /// allocate, and it throws away what the block was in the middle of. A
    /// test counts them to show that an edit which changed nothing for a
    /// block left it alone.
    std::atomic<int> prepares{0};
    /// Whether one was ever prepared after it had run a cycle — which is to
    /// say, while the audio thread could have been inside it. Preparing
    /// reallocates the buffers a cycle reads, so the answer has to be no.
    std::atomic<bool> preparedAfterRunning{false};

    void reset() noexcept
    {
        cycles.store(0);
        cyclesWhenDestroyed.store(0);
        destructions.store(0);
        prepares.store(0);
        preparedAfterRunning.store(false);
    }
};

/// The watch every LifetimeWatchedEffect reports to. One for the process,
/// because the registry makes blocks from a plain factory function and there
/// is nowhere to hand a test's own along. A test resets it before it starts.
[[nodiscard]] LifetimeWatch &lifetimeWatch() noexcept;

/// Half the signal, and a note of how long it lived and when it was made
/// ready.
///
/// Otherwise a \ref FixedGainEffect: a test can put it in the middle of a
/// lane, hear that it is there, and take it out again.
///
/// **It also remembers something across a cycle**, in `memory()`, which
/// preparing throws away — the smallest stand-in for the envelope or the
/// filter history that a real block keeps, and the thing an edit that
/// re-prepared it for no reason would have lost.
class LifetimeWatchedEffect final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.lifetimeWatched";

    explicit LifetimeWatchedEffect(float gain = 0.5f, std::uint32_t channels = 2);
    ~LifetimeWatchedEffect() override;

    LifetimeWatchedEffect(const LifetimeWatchedEffect &) = delete;
    LifetimeWatchedEffect &operator=(const LifetimeWatchedEffect &) = delete;

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return {};
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

    /// How many cycles it has run since it was last prepared. Preparing sets
    /// it back to nought, the way preparing a real block clears what it
    /// remembers.
    [[nodiscard]] std::uint64_t memory() const noexcept
    {
        return m_memory;
    }

private:
    dsp::BlockInfo m_info;
    float m_gain = 0.5f;
    std::uint64_t m_memory = 0;
    /// The watch, found when the block is made. **Not looked up from the
    /// audio thread**: lifetimeWatch() is a function-local static, whose
    /// first use takes a guard that may block, so a cycle that called it
    /// would not be nonblocking — and RealtimeSanitizer's build refuses it.
    LifetimeWatch *m_watch = nullptr;
};

/// An effect that holds the audio thread for a while, every so often.
///
/// **It is an injected defect, and it is the only way to prove that the
/// counter of missed cycles counts them.** A node cannot be shown to notice
/// a lost cycle without losing one, and a lost cycle is a process callback
/// that did not come back in time. So this one spins: a cycle that is late
/// is late whether the thread was working or waiting, and a spin is what a
/// graph too heavy for its quantum actually does.
///
/// It passes the signal through untouched, so what a test hears is the
/// lateness and nothing else. The two figures are read on the audio thread
/// and written from the test, so they are atomic; both at nought is a block
/// that does nothing at all, which is how a test puts the session back in
/// order and listens to it recover.
class StallingEffect final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "effect.stalling";

    explicit StallingEffect(std::uint32_t channels = 2);

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return {};
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

    /// Holds one cycle in \a everyNth for \a microseconds. Either at nought
    /// stops it from the next cycle on.
    void holdEvery(std::uint32_t everyNth, std::uint32_t microseconds) noexcept
    {
        m_everyNth.store(everyNth, std::memory_order_relaxed);
        m_microseconds.store(microseconds, std::memory_order_relaxed);
    }

    /// The same for every StallingEffect that has no hold of its own: for a
    /// test whose blocks the runtime makes, where there is no instance to
    /// hand.
    static void holdAllEvery(std::uint32_t everyNth, std::uint32_t microseconds) noexcept
    {
        s_everyNth.store(everyNth, std::memory_order_relaxed);
        s_microseconds.store(microseconds, std::memory_order_relaxed);
    }

    /// How many cycles it has held up, so a test knows the defect it asked
    /// for was really injected.
    [[nodiscard]] std::uint64_t heldCycles() const noexcept
    {
        return m_held.load(std::memory_order_relaxed);
    }

    /// How many cycles it has run at all.
    [[nodiscard]] std::uint64_t cycleCount() const noexcept
    {
        return m_cycles.load(std::memory_order_relaxed);
    }

private:
    dsp::BlockInfo m_info;
    std::atomic<std::uint32_t> m_everyNth{0};
    std::atomic<std::uint32_t> m_microseconds{0};
    std::atomic<std::uint64_t> m_cycles{0};
    std::atomic<std::uint64_t> m_held{0};
    static inline std::atomic<std::uint32_t> s_everyNth{0};
    static inline std::atomic<std::uint32_t> s_microseconds{0};
};

/// A block that stands where a session's output device would, so that a
/// test can have a lane whose ports face PipeWire outwards. It passes the
/// signal on untouched; what it is for is its category.
class PassThroughOutput final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "output.passThrough";

    explicit PassThroughOutput(std::uint32_t channels = 2);

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return {};
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

private:
    dsp::BlockInfo m_info;
};

} // namespace rb::test

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pipeline.h"

#include "model/resampler.h"

#include <sys/prctl.h>
#include <time.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <vector>

namespace rb::modelhost
{

namespace
{

using Clock = std::chrono::steady_clock;

/// How often the host looks for sound. Waking it from the audio thread
/// would be a system call there.
constexpr long kLookNanoseconds = 1'000'000;
/// How closely the system keeps to that.
constexpr unsigned long kTimerSlackNanoseconds = 50'000;
/// How quickly the shown load follows a run.
constexpr double kLoadSmoothing = 0.1;

void sleepALittle() noexcept
{
    const timespec wait{0, kLookNanoseconds};
    ::clock_nanosleep(CLOCK_MONOTONIC, 0, &wait, nullptr);
}

/// Everything that follows the sound through one direction of the path.
class Path
{
public:
    Path(model::SharedRings &rings,
         const model::Description &description,
         const PipelineOptions &options,
         OnnxRunner &runner)
        : m_rings(rings)
        , m_description(description)
        , m_options(options)
        , m_runner(runner)
        , m_lanes(options.downmix && description.channels == 1 ? 1 : model::kRingChannels)
        , m_stereo(description.channels == 2)
        , m_pathDelay(model::timingFor(description, options.sessionRate, 0)->pathDelay)
        , m_hopSeconds(static_cast<double>(description.hopSize) / description.sampleRate)
    {
        for (std::size_t lane = 0; lane < m_lanes; ++lane) {
            m_down[lane].prepare(options.sessionRate, description.sampleRate);
            m_up[lane].prepare(description.sampleRate, options.sessionRate);
        }
        for (std::vector<float> &history : m_history) {
            history.assign(description.frameSize, 0.0f);
        }
        m_input.resize(std::size_t{description.channels} * description.frameSize);
        m_output.resize(std::size_t{description.channels} * description.hopSize);
    }

    /// Starts the stream again from frame \a start.
    void startAt(std::uint64_t start)
    {
        model::SharedHeader &header = m_rings.header();
        m_next = start;
        m_outAt = start;
        m_dropLeft = m_pathDelay;
        for (std::size_t lane = 0; lane < m_lanes; ++lane) {
            m_down[lane].reset();
            m_up[lane].reset();
            m_atModelRate[lane].clear();
            m_atSessionRate[lane].clear();
        }
        for (std::vector<float> &history : m_history) {
            std::ranges::fill(history, 0.0f);
        }
        // Empty first, so nobody takes the old end for the new start's.
        header.wetEnd.store(start, std::memory_order_release);
        header.wetFrom.store(start, std::memory_order_release);
    }

    /// One turn: what has arrived, through the model, into the rings. False
    /// when the model broke its promise.
    [[nodiscard]] bool turn()
    {
        model::SharedHeader &header = m_rings.header();
        header.beat.fetch_add(1, std::memory_order_relaxed);

        if (const std::uint32_t epoch = header.resetEpoch.load(std::memory_order_relaxed); epoch != m_epoch) {
            m_epoch = epoch;
            m_runner.clearState();
            startAt(header.written.load(std::memory_order_acquire));
        }
        const std::uint64_t written = header.written.load(std::memory_order_acquire);
        if (written - m_next > m_options.latency) {
            // Whatever this would make is already too late: on from now.
            header.skipped.fetch_add(1, std::memory_order_relaxed);
            startAt(written);
            return true;
        }
        if (written == m_next) {
            sleepALittle();
            return true;
        }
        take(written);
        while (m_atModelRate[0].size() >= m_description.hopSize) {
            if (!runOnce()) {
                return false;
            }
        }
        deliver();
        return true;
    }

private:
    /// The new frames of the rings, at the model's rate.
    void take(std::uint64_t written)
    {
        const auto count = static_cast<std::size_t>(written - m_next);
        const std::uint64_t mask = m_rings.capacity() - 1;
        std::array<std::vector<float>, model::kRingChannels> &chunk = m_chunk;
        for (std::uint32_t channel = 0; channel < model::kRingChannels; ++channel) {
            chunk[channel].resize(count);
            const std::span<const float> ring = m_rings.input(channel);
            for (std::size_t frame = 0; frame < count; ++frame) {
                chunk[channel][frame] = ring[(m_next + frame) & mask];
            }
        }
        m_next = written;
        if (m_lanes == 1) {
            for (std::size_t frame = 0; frame < count; ++frame) {
                chunk[0][frame] = 0.5f * (chunk[0][frame] + chunk[1][frame]);
            }
        }
        for (std::size_t lane = 0; lane < m_lanes; ++lane) {
            m_down[lane].process(chunk[lane], m_atModelRate[lane]);
        }
    }

    /// Whether the model's answer in m_output is sound.
    [[nodiscard]] bool isSound() const
    {
        return std::ranges::all_of(m_output, [](float sample) { return std::isfinite(sample); });
    }

    /// One hop: a run for a stereo model or a mixed lane, one a side for a
    /// mono model.
    [[nodiscard]] bool runOnce()
    {
        const std::uint32_t hop = m_description.hopSize;
        const std::uint32_t frame = m_description.frameSize;
        for (std::size_t lane = 0; lane < m_lanes; ++lane) {
            std::vector<float> &history = m_history[lane];
            std::copy(history.begin() + hop, history.end(), history.begin());
            std::copy_n(m_atModelRate[lane].begin(), hop, history.end() - hop);
            m_atModelRate[lane].erase(m_atModelRate[lane].begin(), m_atModelRate[lane].begin() + hop);
        }
        const Clock::time_point before = Clock::now();
        if (m_stereo) {
            std::copy(m_history[0].begin(), m_history[0].end(), m_input.begin());
            std::copy(m_history[1].begin(), m_history[1].end(), m_input.begin() + frame);
            if (!m_runner.run(0, m_input, m_output)) {
                return false;
            }
            silenceIfNotSound();
            m_up[0].process(std::span<const float>{m_output}.first(hop), m_atSessionRate[0]);
            m_up[1].process(std::span<const float>{m_output}.subspan(hop, hop), m_atSessionRate[1]);
        } else {
            for (std::size_t lane = 0; lane < m_lanes; ++lane) {
                if (!m_runner.run(lane, m_history[lane], m_output)) {
                    return false;
                }
                silenceIfNotSound();
                m_up[lane].process(m_output, m_atSessionRate[lane]);
            }
        }
        const double seconds = std::chrono::duration<double>(Clock::now() - before).count();
        m_load += kLoadSmoothing * (seconds / m_hopSeconds - m_load);
        m_rings.header().load.store(static_cast<std::uint32_t>(std::min(m_load * 1000.0, 4.0e9)),
                                    std::memory_order_relaxed);
        return true;
    }

    /// **Not a number is not a sound**: such a run counts as silence.
    void silenceIfNotSound()
    {
        if (!isSound()) {
            std::ranges::fill(m_output, 0.0f);
            m_rings.header().badRuns.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /// What has come out, into the rings, past what the path delays.
    void deliver()
    {
        const std::uint64_t mask = m_rings.capacity() - 1;
        const std::size_t ready = m_atSessionRate[0].size();
        const std::size_t dropped = std::min<std::size_t>(ready, m_dropLeft);
        m_dropLeft -= static_cast<std::uint32_t>(dropped);
        const std::size_t count = ready - dropped;
        for (std::uint32_t channel = 0; channel < model::kRingChannels; ++channel) {
            const std::vector<float> &from = m_atSessionRate[m_lanes == 1 ? 0 : channel];
            const std::span<float> ring = m_rings.output(channel);
            for (std::size_t frame = 0; frame < count; ++frame) {
                ring[(m_outAt + frame) & mask] = from[dropped + frame];
            }
        }
        for (std::size_t lane = 0; lane < m_lanes; ++lane) {
            m_atSessionRate[lane].clear();
        }
        m_outAt += count;
        m_rings.header().wetEnd.store(m_outAt, std::memory_order_release);
    }

    model::SharedRings &m_rings;
    const model::Description &m_description;
    const PipelineOptions &m_options;
    OnnxRunner &m_runner;
    std::size_t m_lanes;
    bool m_stereo;
    std::uint32_t m_pathDelay;
    double m_hopSeconds;
    std::array<model::Resampler, model::kRingChannels> m_down;
    std::array<model::Resampler, model::kRingChannels> m_up;
    std::array<std::vector<float>, model::kRingChannels> m_chunk;
    std::array<std::vector<float>, model::kRingChannels> m_atModelRate;
    std::array<std::vector<float>, model::kRingChannels> m_atSessionRate;
    std::array<std::vector<float>, model::kRingChannels> m_history;
    std::vector<float> m_input;
    std::vector<float> m_output;
    std::uint64_t m_next = 0;
    std::uint64_t m_outAt = 0;
    std::uint32_t m_dropLeft = 0;
    std::uint32_t m_epoch = 0;
    double m_load = 0.0;
};

} // namespace

int runPipeline(model::SharedRings &rings,
                const model::Description &description,
                const PipelineOptions &options,
                OnnxRunner &runner)
{
    ::prctl(PR_SET_TIMERSLACK, kTimerSlackNanoseconds);
    Path path(rings, description, options, runner);
    model::SharedHeader &header = rings.header();
    path.startAt(header.written.load(std::memory_order_acquire));
    header.phase.store(static_cast<std::uint32_t>(model::Phase::Ready), std::memory_order_release);
    while (path.turn()) { }
    return 4;
}

} // namespace rb::modelhost

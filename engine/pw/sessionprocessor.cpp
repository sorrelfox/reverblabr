// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "pw/sessionprocessor.h"

#include "dsp/denormals.h"
#include "rt/clock.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>

namespace rb::pw
{

namespace
{

/// Where a ramp of \a remaining frames towards \a target has got to after
/// \a frames of it, starting from \a gain. Stops at the target.
[[nodiscard]] float
rampedGain(float gain, float target, std::uint32_t remaining, std::uint32_t frames) noexcept RB_NONBLOCKING
{
    if (remaining == 0 || frames >= remaining) {
        return target;
    }
    return gain + (target - gain) * (static_cast<float>(frames) / static_cast<float>(remaining));
}

} // namespace

SessionProcessor::SessionProcessor(std::size_t paramQueueCapacity)
    : m_params(paramQueueCapacity)
{
}

SessionProcessor::~SessionProcessor()
{
    // Whatever is still live is freed here, on the thread that destroys the
    // processor. By now no cycle can be running: the filter is gone.
    delete m_live;
}

LiveGraph *SessionProcessor::exchange(LiveGraph *next) noexcept RB_NONBLOCKING
{
    // Two pointer moves. Nothing is built here and nothing is freed: the
    // graph that comes back is freed by the thread that called us, once this
    // has returned and the audio thread has let go of it.
    // The cycle count, the position and the clock belong to the node, not to
    // the graph, so none of them is disturbed: a cycle missed across a
    // handover is still a cycle missed.
    //
    // **What the delay lines hold goes across**. The graph coming in
    // made its own lines, empty; where it has the same line as the one
    // going out — the same wire held back by the same amount, the same
    // bypassed block — it takes over what that one holds, so an edit does
    // not put a gap into every path held back to line up with another.
    // Bounded copies; this is the one moment both graphs are safe to touch.
    if (m_live != nullptr && next != nullptr && m_live->graph && next->graph) {
        (void)next->graph->carryOverFrom(*m_live->graph);
    }
    LiveGraph *retired = m_live;
    m_live = next;
    // **A new graph is a new chance**: an edit that took the heavy
    // block out is heard at once, and one that did not gives way again
    // within kShedAfter, long before the kernel's limit.
    m_shedding = false;
    m_timedCycles = 0;
    return retired;
}

LiveGraph *SessionProcessor::live() noexcept RB_NONBLOCKING
{
    return m_live;
}

bool SessionProcessor::queueParam(const ParamChange &change) noexcept
{
    return m_params.tryPush(change);
}

std::uint32_t
SessionProcessor::requestFade(std::uint32_t lanes, float target, std::uint32_t frames, std::uint32_t holdFrames)
{
    FadeRequest request;
    request.lanes = lanes;
    request.target = target;
    request.frames = frames;
    request.holdFrames = holdFrames;
    // Never 0: that is what the audio thread reports before anything has
    // been asked of it.
    request.sequence = ++m_fadeSequence;
    m_fade.store(request);
    return request.sequence;
}

SessionTelemetry SessionProcessor::telemetry() const
{
    return m_telemetry.load();
}

void SessionProcessor::resetPeakLoad() noexcept
{
    m_resetPeak.store(true, std::memory_order_relaxed);
}

std::uint32_t SessionProcessor::rateChangeWanted() const noexcept
{
    return m_rateWanted.load(std::memory_order_relaxed);
}

void SessionProcessor::setThreadProbes(ThreadRunReader reader, RealtimeCheck realtime) noexcept
{
    m_readThreadRun = reader;
    m_isRealtime = realtime;
}

void SessionProcessor::process(const CycleInfo &cycle) noexcept RB_NONBLOCKING
{
    const dsp::ScopedDenormalsOff denormalsOff;
    const std::uint64_t started = rt::monotonicNanos();

    ++m_cycles;
    m_unreadOutputs += cycle.unreadOutputs;
    if (m_resetPeak.exchange(false, std::memory_order_relaxed)) {
        m_peakLoad = 0.0f;
    }

    // What counts as a missed cycle. Spike S2 found PipeWire's own flag
    // never set, so the count cannot rest on it, and two figures in the
    // cycle say instead: the graph's cycle counter, whose step is how many
    // cycles the graph ran while this node ran one, and the graph position,
    // which should move on by a cycle's frames each time. Both belong to the
    // driver, so **neither may be compared across a change of driver** —
    // which is what clock.id says, and the only thing it says.
    //
    // The counter and the identity are two different questions and were once
    // asked as one. A counter that moves every cycle then looks like a
    // driver that changes every cycle, the position and the driver's own
    // estimate are suppressed for every cycle along with it, and the count
    // sits at zero through anything.
    const bool driverChanged = m_hasClock && cycle.clockId != m_clockId;
    // Signed on purpose. The counter is 32 bits wide and a session long
    // enough will wrap it; a subtraction that wraps with it still reads as
    // the one step it was. A step backwards is the counter having been
    // started again under us, which is the graph being restarted — it says
    // nothing about cycles, and the position it goes with is a new one.
    const std::int32_t graphAdvance = m_hasClock ? static_cast<std::int32_t>(cycle.graphCycle - m_graphCycle) : 1;
    const bool graphRestarted = graphAdvance < 0;
    if (driverChanged || graphRestarted) {
        ++m_clockChanges;
    }
    // The graph counted cycles this node did not run: it was late, and this
    // is by how many. A step of one is a node that kept up, and a step of
    // none is a server that does not count this way at all, which is no
    // evidence either way rather than evidence of trouble.
    const std::uint32_t skipped =
        !driverChanged && !graphRestarted && graphAdvance > 1 ? static_cast<std::uint32_t>(graphAdvance - 1) : 0;
    m_lostCycles += skipped;
    const bool positionJumped = m_expectedPosition != 0 && cycle.position != m_expectedPosition && !driverChanged
        && !graphRestarted && !cycle.discontinuity;
    const bool driverSaysSo = m_hasClock && !driverChanged && cycle.xrunDuration > m_xrunDuration;
    if (cycle.xrunFlag || skipped > 0 || positionJumped || driverSaysSo) {
        ++m_xruns;
    }
    m_expectedPosition = cycle.position + cycle.frames;
    m_clockId = cycle.clockId;
    m_graphCycle = cycle.graphCycle;
    m_xrunDuration = cycle.xrunDuration;
    m_hasClock = true;

    LiveGraph *live = m_live;

    // A new ramp is picked up at the start of a cycle, so it begins on a
    // frame boundary and the control thread knows when it will be over.
    FadeRequest asked;
    if (m_fade.tryLoad(asked) && asked.sequence != m_activeFade.sequence && asked.sequence != 0) {
        m_activeFade = asked;
        m_fadeRemaining = asked.frames;
        m_holdRemaining = asked.frames > 0 ? asked.holdFrames : 0;
        if (live != nullptr && asked.frames == 0) {
            for (SessionPort &port : live->outputs) {
                if ((asked.lanes & (1U << port.lane)) != 0) {
                    port.gain = asked.target;
                }
            }
        }
        if (asked.frames == 0) {
            m_fadeDone = asked.sequence;
        }
    }

    // Parameter changes are read at the start of the cycle, so one set
    // between two cycles is heard from the first sample of this one. A change
    // for a block this graph does not have is dropped rather than kept: the
    // block it was meant for is gone.
    ParamChange change{};
    for (std::size_t drained = 0; drained <= m_params.capacity() && m_params.tryPop(change); ++drained) {
        if (live == nullptr || change.target >= live->paramTargets.size()) {
            continue;
        }
        if (dsp::Block *block = live->paramTargets[change.target]; block != nullptr) {
            block->applyParam(change.index, change.value);
        }
    }

    if (live == nullptr) {
        // The ports are the shell's, not ours, so it silences them. All that
        // is left here is to say the cycle happened. With no graph there is
        // no rate to disagree with either.
        m_rateWanted.store(0, std::memory_order_relaxed);
        (void)observeRun();
        publish(cycle, 0, false, 0.0f);
        return;
    }

    // A rate change is the one change that needs preparing, and preparing
    // allocates, so it cannot happen here ([ADR-0008]). The node goes silent
    // and asks the engine for a graph made for the new rate.
    const bool rateMatches = std::abs(live->sampleRate - static_cast<double>(cycle.sampleRate)) < 0.5;
    m_rateWanted.store(rateMatches ? 0U : cycle.sampleRate, std::memory_order_relaxed);
    if (!rateMatches) {
        ++m_rateMismatches;
        for (SessionPort &port : live->outputs) {
            if (port.buffer != nullptr) {
                std::fill_n(port.buffer, cycle.frames, 0.0f);
            }
        }
        (void)observeRun();
        publish(cycle, 0, true, 0.0f);
        return;
    }

    // **Given way**: the blocks are not run and the lanes carry
    // silence, which is what lets the loop sleep again. Another quantum is a
    // cycle of another length, which may fit; so is a new graph.
    if (m_shedding && cycle.frames != m_shedFrames) {
        m_shedding = false;
        m_timedCycles = 0;
    }

    std::uint32_t sourcePresent = 0;
    for (SessionPort &port : live->inputs) {
        if (port.buffer != nullptr && port.lane < kMaxLanes) {
            sourcePresent |= 1U << port.lane;
        }
        // A port with no buffer is unlinked, and an unlinked port is silence.
        (void)live->graph->bindExternalInput(port.graphNode, port.graphPort, port.buffer);
    }

    if (!m_shedding) {
        // Timed only while the loop is running long, and summed over those
        // cycles, so a session that gives way can say what cost the most.
        const bool timing =
            m_lastRun >= static_cast<std::uint64_t>(kTimeBlocksAfter.count()) && !live->blockNanos.empty();
        if (timing) {
            if (m_timedCycles == 0) {
                std::ranges::fill(live->blockNanos, 0);
            }
            ++m_timedCycles;
            live->graph->execute(cycle.frames, sourcePresent, live->blockNanos);
        } else {
            m_timedCycles = 0;
            live->graph->execute(cycle.frames, sourcePresent);
        }
    }

    // **A ramp may wait first**, holding the lanes where they are: the
    // part of this cycle before it begins, then the part it ramps for.
    const std::uint32_t holding = m_fadeRemaining > 0 ? std::min(cycle.frames, m_holdRemaining) : 0;
    const std::uint32_t rampingNow = m_fadeRemaining > 0 ? std::min(cycle.frames - holding, m_fadeRemaining) : 0;
    for (SessionPort &port : live->outputs) {
        const bool fading = m_fadeRemaining > 0 && (m_activeFade.lanes & (1U << port.lane)) != 0;
        // A ramp that ends inside this cycle ramps for part of it and holds
        // the target for the rest. Stepping on past the target would send
        // the lane out the other side, which is a click of its own.
        const std::uint32_t held = fading ? holding : 0;
        const std::uint32_t ramping = fading ? rampingNow : 0;
        const float step = ramping > 0 ? (m_activeFade.target - port.gain) / static_cast<float>(m_fadeRemaining) : 0.0f;

        if (port.buffer != nullptr) {
            const std::span<const float> from = m_shedding
                ? std::span<const float>{}
                : live->graph->outputBuffer(port.graphNode, port.graphPort, cycle.frames);
            // Every buffer that exists is written in every cycle: a port the
            // graph has nothing to say through carries silence, not whatever
            // the last user of the memory left there.
            if (from.size() < cycle.frames) {
                std::fill_n(port.buffer, cycle.frames, 0.0f);
            } else if (ramping == 0 && held == 0 && port.gain >= 1.0f) {
                // The ordinary case, and the one an edit must not disturb:
                // the lane is copied sample for sample.
                std::copy_n(from.data(), cycle.frames, port.buffer);
            } else {
                for (std::uint32_t frame = 0; frame < held; ++frame) {
                    port.buffer[frame] = from[frame] * port.gain;
                }
                float gain = port.gain;
                for (std::uint32_t frame = held; frame < held + ramping; ++frame) {
                    port.buffer[frame] = from[frame] * gain;
                    gain += step;
                }
                const float after = ramping > 0 ? m_activeFade.target : port.gain;
                for (std::uint32_t frame = held + ramping; frame < cycle.frames; ++frame) {
                    port.buffer[frame] = from[frame] * after;
                }
            }
        }

        // A ramp runs its course whether or not anything is linked to the
        // lane, or the lane would come back at the wrong gain.
        if (fading) {
            port.gain = rampedGain(port.gain, m_activeFade.target, m_fadeRemaining, ramping);
        }
    }

    if (m_fadeRemaining > 0) {
        m_holdRemaining -= holding;
        m_fadeRemaining -= rampingNow;
        if (m_fadeRemaining == 0) {
            // Land exactly on the target rather than wherever the arithmetic
            // got to, and say the ramp is over.
            for (SessionPort &port : live->outputs) {
                if ((m_activeFade.lanes & (1U << port.lane)) != 0) {
                    port.gain = m_activeFade.target;
                }
            }
            m_fadeDone = m_activeFade.sequence;
        }
    }

    const std::uint64_t elapsed = rt::monotonicNanos() - started;
    const double period = cycle.frames > 0 && cycle.sampleRate > 0
        ? static_cast<double>(cycle.frames) * 1'000'000'000.0 / static_cast<double>(cycle.sampleRate)
        : 0.0;
    const float load = period > 0.0 ? static_cast<float>(static_cast<double>(elapsed) / period) : 0.0f;

    // **Give way before the kernel's limit**, if the loop has run
    // long without sleeping — from one cycle into the next with no rest in
    // between, not merely one long cycle — and this session is a real part
    // of why, or if it has run so long that everybody must. Only a
    // real-time thread is held to the limit, so only one gives way; the
    // question is asked only here, where it matters.
    //
    // **A thread that is not real-time goes on**: rtkit can refuse,
    // and nothing holds that thread to a limit. The occasion is counted, so
    // whoever expected the session to give way can tell why it did not; the
    // question is asked once an occasion, not once a cycle.
    const std::uint64_t run = observeRun();
    const bool longRun = m_runAcrossCycles && run >= static_cast<std::uint64_t>(kShedAfter.count())
        && (load >= kShedShare || run >= static_cast<std::uint64_t>(kShedRegardless.count()));
    if (!longRun) {
        m_inRunNotRealtime = false;
    } else if (!m_shedding && !m_inRunNotRealtime) {
        m_realtime = m_isRealtime();
        m_realtimeKnown = true;
        if (m_realtime) {
            m_shedding = true;
            m_shedFrames = cycle.frames;
            ++m_overloads;
            noteHeaviest(*live, period);
        } else {
            m_inRunNotRealtime = true;
            ++m_longRunsNotRealtime;
        }
    }
    publish(cycle, sourcePresent, true, load);
}

std::uint64_t SessionProcessor::observeRun() noexcept RB_NONBLOCKING
{
    // At the end of the cycle, so the run the watch counts from a sleep is
    // this cycle's own work and not the rest of the one before. A reading
    // the kernel will not give is taken as a rest: that is the watch
    // standing still, not the loop.
    rt::ThreadRunReading reading;
    if (!m_readThreadRun(reading)) {
        m_lastRun = 0;
        m_runAcrossCycles = false;
        return 0;
    }
    m_lastRun = m_runWatch.update(reading);
    m_runAcrossCycles = !m_runWatch.sleptSinceLastReading();
    return m_lastRun;
}

void SessionProcessor::noteHeaviest(const LiveGraph &live, double period) noexcept RB_NONBLOCKING
{
    m_heaviest = {};
    m_heaviestLoad = {};
    if (m_timedCycles == 0 || period <= 0.0) {
        return;
    }
    const double cycles = static_cast<double>(m_timedCycles);
    std::array<std::size_t, kOverloadBlocks> picked{};
    std::size_t count = 0;
    for (std::size_t node = 0; node < live.blockNanos.size(); ++node) {
        const std::uint64_t nanos = live.blockNanos[node];
        if (nanos == 0) {
            continue;
        }
        // Insertion into a list of three, heaviest first.
        std::size_t at = count;
        while (at > 0 && live.blockNanos[picked[at - 1]] < nanos) {
            if (at < kOverloadBlocks) {
                picked[at] = picked[at - 1];
            }
            --at;
        }
        if (at < kOverloadBlocks) {
            picked[at] = node;
            count = std::min(count + 1, kOverloadBlocks);
        }
    }
    for (std::size_t index = 0; index < count; ++index) {
        const std::string_view id = live.graph->nodeId(picked[index]);
        const std::size_t length = std::min(id.size(), kOverloadIdBytes - 1);
        std::copy_n(id.data(), length, m_heaviest[index].data());
        m_heaviest[index][length] = '\0';
        m_heaviestLoad[index] =
            static_cast<float>(static_cast<double>(live.blockNanos[picked[index]]) / cycles / period);
    }
}

void SessionProcessor::publish(const CycleInfo &cycle,
                               std::uint32_t sourcePresent,
                               bool hasGraph,
                               float load) noexcept RB_NONBLOCKING
{
    m_peakLoad = std::max(m_peakLoad, load);
    // Asked once, on the first cycle, so a reader knows from the start
    // whether this thread will ever be held to the kernel's limit.
    if (!m_realtimeKnown) {
        m_realtime = m_isRealtime();
        m_realtimeKnown = true;
    }

    SessionTelemetry snapshot;
    snapshot.cycles = m_cycles;
    snapshot.xruns = m_xruns;
    snapshot.rateMismatches = m_rateMismatches;
    snapshot.unreadOutputs = m_unreadOutputs;
    snapshot.peakLoad = m_peakLoad;
    snapshot.lastLoad = load;
    snapshot.frames = cycle.frames;
    snapshot.sampleRate = cycle.sampleRate;
    snapshot.sourcePresent = sourcePresent;
    snapshot.hasGraph = hasGraph ? 1U : 0U;
    snapshot.clockChanges = m_clockChanges;
    snapshot.lostCycles = m_lostCycles;
    snapshot.fadeDone = m_fadeDone;
    snapshot.overloads = m_overloads;
    snapshot.shedding = m_shedding ? 1U : 0U;
    snapshot.realtime = m_realtime ? 1U : 0U;
    snapshot.longRunsNotRealtime = m_longRunsNotRealtime;
    snapshot.heaviest = m_heaviest;
    snapshot.heaviestLoad = m_heaviestLoad;
    m_telemetry.store(snapshot);
}

} // namespace rb::pw

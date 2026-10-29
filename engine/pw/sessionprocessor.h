// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "graph/compiledgraph.h"
#include "rt/realtime.h"
#include "rt/runwatch.h"
#include "rt/seqlock.h"
#include "rt/spscqueue.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace rb::pw
{

/// The most lanes a session node reports "source present" for, one bit each.
/// A lane is a block's ports, so this is a limit on blocks with ports, not on
/// channels or on the size of the graph.
inline constexpr std::uint32_t kMaxLanes = 32;

/// **When a session gives way to keep its data loop alive**. The
/// kernel kills a real-time thread that has used 200 ms of CPU without
/// sleeping (RLIMIT_RTTIME, rtkit's limit), and a data loop whose every
/// cycle overruns never sleeps. So once the loop has run this long without a
/// sleep, a session that is taking a real part of each cycle stops running
/// its blocks and puts out silence: its cycles are then short, the thread
/// sleeps, and the process lives to say why.
inline constexpr std::chrono::nanoseconds kShedAfter = std::chrono::milliseconds{50};
/// The share of a cycle a session must be taking for it to be the one that
/// gives way. Several sessions share a data loop, and a light one going
/// silent would not give the loop back its sleep.
inline constexpr float kShedShare = 0.25f;
/// Past this, every session on the loop gives way whatever its share: ten
/// sessions each taking a tenth of the loop are as fatal as one taking all.
inline constexpr std::chrono::nanoseconds kShedRegardless = std::chrono::milliseconds{100};
/// Once the loop has run this long, a cycle times its blocks, so the ones
/// that cost the most can be named when the session gives way.
inline constexpr std::chrono::nanoseconds kTimeBlocksAfter = std::chrono::milliseconds{25};
/// How many blocks an overload names, heaviest first, and how much room each
/// id gets, closing zero included.
inline constexpr std::size_t kOverloadBlocks = 3;
inline constexpr std::size_t kOverloadIdBytes = 32;

/// One of the node's PipeWire ports, and the port of the graph it carries.
///
/// The handle is the filter's port object. Nothing here knows what it is:
/// the shell around this resolves it to a buffer before each cycle, which is
/// what keeps PipeWire out of the part that has to be testable offline.
struct SessionPort {
    void *handle = nullptr;
    /// Where in the compiled graph the port's audio goes, or comes from.
    std::size_t graphNode = 0;
    std::uint32_t graphPort = 0;
    /// Which lane it belongs to, for the source-present flags.
    std::uint32_t lane = 0;
    /// This cycle's buffer, or null when the port is unlinked. An input with
    /// no buffer is silence; an output with none is skipped.
    float *buffer = nullptr;
    /// What an **output** port is currently multiplied by, which is how a
    /// lane is faded across an edit. A lane that is not being faded sits at
    /// 1 and is copied through untouched, sample for sample. A graph handed
    /// over with a lane at 0 is one whose lane is about to be faded in.
    float gain = 1.0f;
};

/// Everything one cycle needs, handed over as a unit.
///
/// The audio thread never builds one of these and never takes one apart: the
/// engine thread compiles a graph, gathers the ports that go with it, and
/// swaps the whole thing in between two cycles ([ADR-0008]).
struct LiveGraph {
    std::unique_ptr<graph::CompiledGraph> graph;
    std::vector<SessionPort> inputs;
    std::vector<SessionPort> outputs;
    /// The blocks a queued parameter change can address, by slot. A change
    /// for a slot this graph does not have is dropped, which is what makes
    /// the queue safe across an edit that removes a block.
    std::vector<dsp::Block *> paramTargets;
    /// The rate the blocks in it were prepared for. A cycle at another rate
    /// is silence until the engine has prepared a graph for the new one.
    double sampleRate = 48000.0;
    /// How many lanes the ports were laid out in.
    std::uint32_t laneCount = 0;
    /// Where a cycle that is running long times each block, one
    /// entry per node of the graph's spec. Whoever builds the graph sizes
    /// it, so the audio thread never has to; left empty, nothing is timed
    /// and an overload names no block.
    std::vector<std::uint64_t> blockNanos;
};

/// What PipeWire says about the cycle that is about to run.
struct CycleInfo {
    std::uint32_t frames = 0;
    std::uint32_t sampleRate = 48000;
    /// The graph position this cycle starts at, in frames. A cycle that does
    /// not start where the last one ended means cycles were missed — unless
    /// the driver changed under it, which moves the position legitimately.
    std::uint64_t position = 0;
    /// Which clock the position is against. A change of driver changes it,
    /// and then the position before and after cannot be compared. **This is
    /// the whole of what "another driver" means**; nothing else in the cycle
    /// says it.
    std::uint32_t clockId = 0;
    /// The graph's own cycle counter. SPA documents it as counting how often
    /// the graph has been started; this server counts **cycles** with it, so
    /// it goes up by one every time and its step says how many cycles the
    /// graph ran while this node ran one. Reading it as an identity rather
    /// than as a counter is what silenced the xrun count.
    std::uint32_t graphCycle = 0;
    /// The driver's own estimate of how much time has been lost to xruns,
    /// accumulated. It going up is the server saying so itself.
    std::uint64_t xrunDuration = 0;
    /// Whether PipeWire set SPA_IO_CLOCK_FLAG_XRUN_RECOVER. Spike S2 never
    /// saw it set, so it is taken as one sign among several rather than the
    /// sign.
    bool xrunFlag = false;
    /// Whether the driver said its timestamps jumped (SPA_IO_CLOCK_FLAG_DISCONT).
    /// That is a discontinuity, not a missed deadline, so it is not an xrun.
    bool discontinuity = false;
    /// How many output ports still held the buffer written the cycle before,
    /// and so were given none this cycle.
    std::uint32_t unreadOutputs = 0;
};

/// Ask the audio thread to ramp some output lanes up or down.
///
/// **This is how an edit is made not to click.** A graph that changes what
/// a lane carries cannot simply be swapped in: the signal would step from
/// whatever the old graph was putting out to whatever the new one puts out.
/// So the lane is ramped down first, the graph is swapped while the lane is
/// silent, and the lane is ramped back up. Lanes the edit does not touch are
/// left at 1 throughout and go on sample for sample ([ADR-0008]).
///
/// The audio thread cannot be asked to wait, so the control thread watches
/// SessionTelemetry::fadeDone for its own sequence number to come back.
struct FadeRequest {
    /// One bit per output lane. A lane not named here is not touched.
    std::uint32_t lanes = 0;
    /// Where the ramp ends: 0 to fade a lane out, 1 to fade it back in.
    float target = 1.0f;
    /// How long the ramp is. Zero applies the target at once, which is what
    /// a test that does not care about clicks asks for.
    std::uint32_t frames = 0;
    /// The control thread's own count, echoed back in the telemetry once the
    /// ramp has finished. Never 0, which means "nothing asked for".
    std::uint32_t sequence = 0;
};

/// A parameter change on its way to the audio thread.
struct ParamChange {
    /// Index into LiveGraph::paramTargets.
    std::uint32_t target = 0;
    /// Index into that block's params().
    std::uint32_t index = 0;
    float value = 0.0f;
};

/// What the node reports about itself. Read through a seqlock, so a reader
/// always sees one cycle's figures and never half of two.
struct SessionTelemetry {
    /// Cycles run since the node started.
    std::uint64_t cycles = 0;
    /// How many times cycles were missed: the graph's cycle counter moved on
    /// by more than one, or the position did not follow on from the one
    /// before, or the driver's own xrun estimate went up, or PipeWire
    /// flagged a recovery — with a change of driver, a restart of the graph
    /// and a discontinuity ruled out. It counts **occasions**, so one stall
    /// that costs a hundred cycles is one.
    std::uint64_t xruns = 0;
    /// How many cycles those occasions cost altogether, as the graph's own
    /// counter measures them: the magnitude behind xruns.
    std::uint64_t lostCycles = 0;
    /// Cycles that arrived at a rate the live graph was not prepared for,
    /// and so came out silent.
    std::uint64_t rateMismatches = 0;
    /// How many times an output port still held the buffer it wrote the
    /// cycle before, because the server had not passed it on, and so was
    /// not given another behind it. Each is one cycle that channel
    /// played a cycle late, once, instead of for as long as its link lasted.
    std::uint64_t unreadOutputs = 0;
    /// The worst cycle since the figure was last reset, as a fraction of the
    /// cycle's own period: 0.25 means a quarter of the time available.
    float peakLoad = 0.0f;
    /// The same for the cycle just run.
    float lastLoad = 0.0f;
    /// The last cycle's length and rate, which is PipeWire's quantum.
    std::uint32_t frames = 0;
    std::uint32_t sampleRate = 0;
    /// One bit per lane: set when a port of that lane had a buffer in the
    /// cycle just run. The Recorder's gap handling reads it to tell silence
    /// from a source that has gone away.
    std::uint32_t sourcePresent = 0;
    /// Whether a graph was live for the cycle just run.
    std::uint32_t hasGraph = 0;
    /// How often the node has been handed to another driver (the clock id
    /// changed), or the graph has been restarted under it (its cycle counter
    /// went backwards). Each of those moves the position without a cycle
    /// having been missed, so each is a cycle the counters above ignore.
    std::uint32_t clockChanges = 0;
    /// The sequence number of the last fade that finished. The control
    /// thread swaps the graph when its own number comes back.
    std::uint32_t fadeDone = 0;
    /// How much the session holds the sound back, in samples at the graph's
    /// rate: its longest path's latency, which the node reports to the
    /// audio system as its process latency. Filled in by the node,
    /// not the audio thread.
    std::uint32_t processLatency = 0;
    /// How many times the node has given way to keep its data loop alive:
    /// stopped running its blocks and put out silence, because the
    /// loop had run kShedAfter without sleeping.
    std::uint32_t overloads = 0;
    /// Whether it is silent for that reason now. It runs its blocks again
    /// with the next graph it is given, or at another quantum.
    std::uint32_t shedding = 0;
    /// Whether the data thread was real-time when the node last asked: on
    /// its first cycle, and each time the loop ran long enough to give way.
    /// rtkit can refuse (its burst limit), and a thread it refused is
    /// held to no time limit, so it has nothing to give way to.
    std::uint32_t realtime = 0;
    /// How many times the loop ran long enough to give way on a thread that
    /// was not real-time, and so rightly went on. It counts
    /// occasions: one run that never sleeps is one, however long it goes.
    std::uint32_t longRunsNotRealtime = 0;
    /// The blocks that cost the most in the cycles before the last time it
    /// gave way, heaviest first, by their ids in the graph, zero-padded; an
    /// empty id where there were fewer, or where the loop went from short
    /// cycles to fatal ones too quickly to time any.
    std::array<std::array<char, kOverloadIdBytes>, kOverloadBlocks> heaviest{};
    /// Each one's share of a cycle in those cycles: 1.5 is half as long
    /// again as the cycle it had.
    std::array<float, kOverloadBlocks> heaviestLoad{};
};

/// The real-time half of a session node: everything one audio cycle does,
/// with no PipeWire in it.
///
/// It is separate from SessionNode so that the part which has to obey the
/// [real-time rules](../../docs/architecture.md#real-time-rules) can be run,
/// and checked, without an audio server anywhere near it — by the allocation
/// trap under GCC and by RealtimeSanitizer under Clang.
///
/// **Who calls what.** process() and exchange() run on PipeWire's data
/// thread; everything else runs on the engine thread. queueParam() is the one
/// crossing, and it is a lock-free queue.
class SessionProcessor
{
public:
    /// \a paramQueueCapacity is how many parameter changes may be in flight
    /// at once. Building the queue allocates, which is why it happens here
    /// and not in a cycle.
    explicit SessionProcessor(std::size_t paramQueueCapacity = 1024);
    ~SessionProcessor();

    SessionProcessor(const SessionProcessor &) = delete;
    SessionProcessor &operator=(const SessionProcessor &) = delete;

    /// Puts \a next in place and hands back what was there.
    ///
    /// **Called on the data thread**, between two cycles, through a blocking
    /// `pw_loop_invoke()`. It only moves pointers: nothing is built and
    /// nothing is freed here. When the invoke returns, the graph that came
    /// back is out of use and the engine thread frees it.
    ///
    /// The pointers are raw rather than owning because a smart pointer's
    /// destructor is a call to free, which is the one thing this function
    /// must never do. Ownership passes in at the call and out at the return;
    /// the engine thread holds it either side.
    [[nodiscard]] LiveGraph *exchange(LiveGraph *next) noexcept RB_NONBLOCKING;

    /// The graph the cycles are running, or null. Data thread only: the shell
    /// walks its ports to fill in this cycle's buffers.
    [[nodiscard]] LiveGraph *live() noexcept RB_NONBLOCKING;

    /// One cycle. Every port's buffer must already be filled in.
    void process(const CycleInfo &cycle) noexcept RB_NONBLOCKING;

    /// Queues a parameter change for the audio thread. False when the queue
    /// is full, which the caller reports rather than retries: a value that
    /// late is already out of date.
    [[nodiscard]] bool queueParam(const ParamChange &change) noexcept;

    /// Asks for some lanes to be ramped, and returns the sequence number to
    /// watch for in telemetry().fadeDone. A request replaces any earlier one
    /// that has not finished.
    [[nodiscard]] std::uint32_t requestFade(std::uint32_t lanes, float target, std::uint32_t frames);

    [[nodiscard]] SessionTelemetry telemetry() const;

    /// Starts the peak load figure again from the next cycle. The telemetry
    /// sampler does this each period, so the peak means "since you last
    /// looked" rather than "since the session started".
    void resetPeakLoad() noexcept;

    /// The rate PipeWire has started running at while the live graph was
    /// prepared for another, or 0 when they agree. The engine watches this,
    /// prepares a graph for the new rate and swaps it in; the audio thread
    /// never re-prepares anything itself.
    [[nodiscard]] std::uint32_t rateChangeWanted() const noexcept;

    /// Where the audio thread reads how long its thread has run without
    /// sleeping, and whether the thread is real-time; rt::readThreadRun()
    /// and rt::callingThreadIsRealtime() unless a test says otherwise. Set
    /// before the first cycle.
    using ThreadRunReader = bool (*)(rt::ThreadRunReading &) noexcept RB_NONBLOCKING;
    using RealtimeCheck = bool (*)() noexcept RB_NONBLOCKING;
    void setThreadProbes(ThreadRunReader reader, RealtimeCheck realtime) noexcept;

private:
    void
    publish(const CycleInfo &cycle, std::uint32_t sourcePresent, bool hasGraph, float load) noexcept RB_NONBLOCKING;

    /// Reads how long the loop has run without sleeping, once a cycle, at
    /// its end, and keeps it for the next cycle's start.
    std::uint64_t observeRun() noexcept RB_NONBLOCKING;

    /// Picks the blocks that cost the most from \a live's timings, over the
    /// cycles timed so far, each as a share of a \a period nanosecond cycle.
    void noteHeaviest(const LiveGraph &live, double period) noexcept RB_NONBLOCKING;

    /// Data thread only, apart from construction and destruction.
    LiveGraph *m_live = nullptr;
    std::uint64_t m_expectedPosition = 0;
    std::uint64_t m_cycles = 0;
    std::uint64_t m_xruns = 0;
    std::uint64_t m_lostCycles = 0;
    std::uint64_t m_rateMismatches = 0;
    std::uint64_t m_unreadOutputs = 0;
    std::uint32_t m_clockChanges = 0;
    std::uint32_t m_clockId = 0;
    std::uint32_t m_graphCycle = 0;
    std::uint64_t m_xrunDuration = 0;
    bool m_hasClock = false;
    float m_peakLoad = 0.0f;

    /// The ramp the audio thread is applying, or nothing. Written by the
    /// control thread, read at the start of every cycle.
    rt::SeqlockValue<FadeRequest> m_fade;
    /// Data thread only: the request being worked through, and what is left
    /// of it.
    FadeRequest m_activeFade;
    std::uint32_t m_fadeRemaining = 0;
    std::uint32_t m_fadeDone = 0;
    std::uint32_t m_fadeSequence = 0;

    rt::SpscQueue<ParamChange> m_params;
    rt::SeqlockValue<SessionTelemetry> m_telemetry;
    std::atomic<bool> m_resetPeak{false};
    std::atomic<std::uint32_t> m_rateWanted{0};

    /// Data thread only: how long the loop has run without sleeping, and
    /// whether this node has given way to it.
    ThreadRunReader m_readThreadRun = &rt::readThreadRun;
    RealtimeCheck m_isRealtime = &rt::callingThreadIsRealtime;
    rt::RunWatch m_runWatch;
    std::uint64_t m_lastRun = 0;
    bool m_runAcrossCycles = false;
    bool m_shedding = false;
    std::uint32_t m_shedFrames = 0;
    std::uint32_t m_overloads = 0;
    /// Data thread only: what the thread was when last asked, and the long
    /// runs it went on through for not being real-time.
    bool m_realtimeKnown = false;
    bool m_realtime = false;
    bool m_inRunNotRealtime = false;
    std::uint32_t m_longRunsNotRealtime = 0;
    std::uint32_t m_timedCycles = 0;
    std::array<std::array<char, kOverloadIdBytes>, kOverloadBlocks> m_heaviest{};
    std::array<float, kOverloadBlocks> m_heaviestLoad{};
};

} // namespace rb::pw

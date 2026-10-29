// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/xrunhistory.h"
#include "rt/meter.h"
#include "rt/seqlock.h"

#include <QObject>
#include <QString>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <vector>

namespace rb::core
{

class SessionRuntime;

/// The most blocks one session is metered on. A session with more than this
/// many blocks still runs and is still heard; only the later blocks have no
/// meter.
inline constexpr std::size_t kMaxMeteredBlocks = 64;
/// How much room a block id gets in a snapshot, in bytes, including the
/// closing zero.
inline constexpr std::size_t kMaxTelemetryIdBytes = 32;

/// How many bands a live spectrum has: dsp::SpectrumAnalyser's,
/// spaced evenly in octaves from 20 Hz to 20 kHz.
inline constexpr std::size_t kSpectrumBands = 96;

/// **Both sides of the analysed block, mixed**, as a person hears them
/// together: what analyse() takes unless it is told one channel.
inline constexpr int kBothChannels = -1;

/// Where live-spectrum band \a band is centred, in Hz. **Asked of the
/// analyser**, so the interface cannot place a band anywhere it was not
/// measured.
[[nodiscard]] double spectrumBandCentreHz(std::size_t band) noexcept;

/// What was measured about one block since the last snapshot.
///
/// **Amplitudes, not decibels.** Turning a level into dB is a logarithm per
/// value, and the interface has to do its own arithmetic on the way to a
/// meter's height anyway; doing it here would be work done twice, once at
/// 60 Hz whether anybody is looking or not.
struct BlockTelemetry {
    /// The block's id, as the session spells it, zero-padded. An id longer
    /// than this is cut, so the interface matches on what it finds here
    /// rather than the other way about.
    char id[kMaxTelemetryIdBytes]{};
    std::uint32_t channelCount = 0;
    float peak[rt::kMaxMeterChannels]{};
    float rms[rt::kMaxMeterChannels]{};
    /// What the block publishes about itself: a gain reduction, an input
    /// level. What each one means is the block's business and its face's.
    float values[rt::kMaxMeterValues]{};
    /// Whether any channel reached full scale since the last snapshot. The
    /// interface latches it for as long as it wants to show it.
    bool clipped = false;
    /// Whether there was a signal on any channel: what the wires and the
    /// state dots go by.
    bool active = false;
    /// How late the block makes the sound, in samples at the snapshot's
    /// rate. Nought for nearly every block.
    std::uint32_t latencySamples = 0;
    /// Whether the block is switched on.
    ///
    /// **Off does not mean quiet, and quiet does not mean off.** A block in
    /// the middle of a lane that is switched off still passes the signal and
    /// still meters it, so it reads as active; one at the end of a lane is
    /// silenced instead and reads as quiet. The interface needs both
    /// facts, and shows them differently.
    bool enabled = true;
};

/// One session's telemetry, as the interface reads it. Named a snapshot
/// and not a telemetry to keep it apart from pw::SessionTelemetry, which is
/// what the node says about itself and one of the things in here.
///
/// **A plain old struct on purpose.** It is published through a seqlock in
/// a shared-memory region (ADR-0018), which a window in another process maps
/// and reads exactly as a window in this one does; nothing in it owns
/// anything, so where the bytes live changes nothing about what they are.
struct TelemetrySnapshot {
    /// Counts up once per snapshot. A reader that sees the same number twice
    /// has read the same snapshot twice, and one that sees it jump has
    /// missed some, which for a meter is nothing to worry about.
    std::uint64_t frame = 0;
    /// When it was taken, on the monotonic clock. The interface's peak hold
    /// and decay are in wall-clock time, so they need this rather than a
    /// count of frames.
    std::uint64_t nanos = 0;

    /// What the node itself has to say: how long a cycle is taking and how
    /// often it has been late.
    float dspLoad = 0.0f; ///< The last cycle's share of its deadline, 0…1.
    float peakDspLoad = 0.0f;
    std::uint32_t xruns = 0;
    std::uint32_t sampleRate = 0;
    std::uint32_t frames = 0; ///< The quantum the last cycle ran at.
    /// How late the session makes the sound, in samples: its longest path's
    /// latency, which is what it tells the audio system.
    std::uint32_t processLatency = 0;
    bool running = false;
    /// **The one block whose spectrum is being taken**, zero-padded,
    /// or empty: whichever block's curve somebody is looking at, and
    /// nothing is worked out for the rest. Its spectrum is spectrumBands
    /// levels in dB against a full-scale sine, low to high.
    char analysedId[kMaxTelemetryIdBytes]{};
    /// Which of its channels the spectra are of: kBothChannels, or 0 for
    /// the first and 1 for the second — the ear of a block that
    /// works on one ear.
    std::int32_t analysedChannel = kBothChannels;
    std::uint32_t spectrumBands = 0;
    float spectrum[kSpectrumBands]{};
    /// **The same block's spectrum before it**, in the same bands
    /// and from the same moment, so that a panel can draw what the block
    /// does as the difference. Nought bands for a block with no inputs.
    std::uint32_t inputSpectrumBands = 0;
    float inputSpectrum[kSpectrumBands]{};
    /// **What the analysed block says about each of its bands**,
    /// such as the gain it applies in each: bandValueCount of them, from
    /// the last cycle it published. Nought until it has published since
    /// somebody started looking.
    std::uint32_t bandValueCount = 0;
    float bandValues[rt::kMaxBandValues]{};

    std::uint32_t blockCount = 0;
    BlockTelemetry blocks[kMaxMeteredBlocks]{};
};

static_assert(std::is_trivially_copyable_v<TelemetrySnapshot>, "a snapshot is copied byte for byte");

/// Where a session's telemetry is published, and what the interface holds a
/// pointer to.
using TelemetrySlot = rt::SeqlockValue<TelemetrySnapshot>;

/// What a block's meter last actually measured, and when.
///
/// **A drain that measured nothing is no news, not silence.** The audio
/// thread accumulates and the sampler empties, so a tick that lands between
/// two audio cycles takes an empty accumulator — zero peak, zero frames —
/// and publishing that as a level says the session went quiet when all that
/// happened is that nobody has played since the last look. The sampler runs
/// every 16 ms and a cycle at a quantum of 1024 takes 21.3 ms, so about one
/// tick in four measured nothing: the state dots dropped out of live, the
/// meters fell to the floor and every wire went grey, sixteen times a
/// second. What was measured last is carried forward instead.
struct HeldLevels {
    std::array<float, rt::kMaxMeterChannels> peak{};
    std::array<float, rt::kMaxMeterChannels> rms{};
    /// When the last cycle's worth of samples arrived, by the monotonic
    /// clock the snapshots are stamped with. Zero for a block nothing has
    /// been measured on yet.
    std::uint64_t measuredAt = 0;
    /// Which channels have been measured since the block last started, a
    /// bit a channel. A block is shown only once every channel has.
    std::uint32_t measuredChannels = 0;
    /// Whether this block was seen in the tick being taken, so that the
    /// blocks an edit removed do not sit here for the life of the session.
    bool seen = false;
};

/// Folds one tick's readings of a block's channels into what is published
/// for it, carrying \a held forward: a tick that measured nothing keeps
/// the last levels for up to \a holdNanos, and a block is shown only once
/// every one of its channels has been measured. **The sampler's own
/// arithmetic**, apart so that a test can hand it readings that a real
/// audio thread produces only now and then.
void foldReadings(HeldLevels &held,
                  std::span<const rt::MeterReading> readings,
                  std::uint64_t nanos,
                  std::uint64_t holdNanos,
                  BlockTelemetry &into);

/// **What is worked out for the one block being analysed**:
/// its spectra after and before it, from the rings its meter fills while it
/// is analysed, and the band values it publishes. The sampler's own work,
/// apart so that a test can run it over a graph played offline.
///
/// Engine thread. It never switches a meter's copying on or off itself: the
/// sampler does that, knowing which meters its runtime still has.
class BlockAnalysis
{
public:
    BlockAnalysis();
    ~BlockAnalysis();
    BlockAnalysis(const BlockAnalysis &) = delete;
    BlockAnalysis &operator=(const BlockAnalysis &) = delete;

    /// Takes \a meter's spectra from now on, or nobody's for null: forgets
    /// what its rings held from an earlier look and starts both averages
    /// again. \a meter must already be analysing.
    void follow(rt::BlockMeter *meter, double sampleRate);
    [[nodiscard]] rt::BlockMeter *following() const noexcept;

    /// Which channel the spectra are of: kBothChannels, 0 or 1. A change
    /// starts both averages again, since what they held was of another
    /// channel.
    void setChannel(int channel);
    [[nodiscard]] int channel() const noexcept;

    /// Drains what the block's rings hold into the two spectra and fills
    /// \a snapshot's analysed fields with them and with the block's band
    /// values. Does nothing while nobody is followed.
    void take(double sampleRate, TelemetrySnapshot &snapshot);

private:
    class Private;
    std::unique_ptr<Private> d;
};

/// Reads the accumulators and publishes a snapshot, sixty times a second.
///
/// **It runs on the engine thread, not the audio thread.** The audio thread
/// only ever adds to an accumulator; emptying one, working out an RMS and
/// laying out a snapshot all happen here, where taking a few microseconds is
/// nobody's problem.
///
/// **Sixty a second, not one per cycle.** A cycle is 47 times a second at
/// quantum 2048 and 1500 times a second at quantum 32; a meter that followed
/// the cycle would stutter at one end and waste the whole screen's worth of
/// work at the other. The accumulators mean the rate the interface is read
/// at and the rate the audio runs at have nothing to do with each other.
class TelemetrySampler : public QObject
{
    Q_OBJECT

public:
    /// How often a snapshot is taken. Sixty a second, because that is what a
    /// screen does; a display that draws faster reads the same snapshot
    /// twice, which is what the frame number is for.
    static constexpr std::chrono::milliseconds kPeriod{16};

    explicit TelemetrySampler(QObject *parent = nullptr);
    ~TelemetrySampler() override;

    /// Follows \a runtime, or nothing at all when it is null, which is what
    /// a session that has been stopped looks like. The runtime must outlive
    /// the sampler or be taken away with another call.
    void watch(SessionRuntime *runtime);

    /// **Takes \a blockId's spectra and band values from the next snapshot
    /// on**, or nobody's when it is empty. One block at a time:
    /// the one whose curve somebody has open. The block's input and output
    /// are copied out for it only from now on, and for no block while
    /// nobody asks. \a channel is kBothChannels, or the one channel the
    /// spectra are to be of.
    void analyse(const QString &blockId, int channel = kBothChannels);
    [[nodiscard]] QString analysed() const;
    [[nodiscard]] int analysedChannel() const;

    /// Starts and stops the timer. A sampler with nothing to watch publishes
    /// one empty snapshot and then stops on its own.
    void start();
    void stop();
    [[nodiscard]] bool isRunning() const;

    /// Takes a snapshot now rather than when the timer next fires. **This is
    /// what a test uses**: it needs no timer and no waiting, and it is the
    /// same code the timer runs.
    void sample();

    /// Where the snapshots are published. The interface keeps a pointer to
    /// this and reads it on its own frame tick.
    [[nodiscard]] const TelemetrySlot &slot() const;

    /// **The same slot, for a window in another process** (ADR-0018): a new
    /// descriptor, read-only, for the shared memory slot() lives in. The
    /// caller owns it. -1 if the slot could not be put in shared memory,
    /// which leaves it readable in this process only.
    [[nodiscard]] int shareReadOnly() const;

    /// **When this session was late**: each time the node's count
    /// of late cycles went up, as the samples saw it, the last
    /// XrunHistory::kCapacity of them. It outlasts a stop and a start, so a
    /// session that was stopped because it kept dropping out still says
    /// when it did.
    [[nodiscard]] std::vector<XrunEvent> xrunHistory() const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::core

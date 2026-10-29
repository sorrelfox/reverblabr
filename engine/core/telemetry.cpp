// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/telemetry.h"

#include "core/sessionruntime.h"
#include "core/sharedtelemetry.h"
#include "dsp/audioblock.h"
#include "dsp/spectrum.h"
#include "pw/sessionnode.h"
#include "rt/clock.h"

#include <QDateTime>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace rb::core
{

namespace
{

/// Copies a block id into the fixed room a snapshot gives it. An id longer
/// than that is cut rather than left out: half an id still tells a person
/// which block is complaining, and the interface matches on what it finds.
void writeId(char (&into)[kMaxTelemetryIdBytes], const QString &id)
{
    const QByteArray utf8 = id.toUtf8();
    const auto length = std::min(static_cast<std::size_t>(utf8.size()), kMaxTelemetryIdBytes - 1);
    std::memcpy(into, utf8.constData(), length);
    into[length] = '\0';
}

} // namespace

double spectrumBandCentreHz(std::size_t band) noexcept
{
    return dsp::SpectrumAnalyser::bandCentreHz(band);
}

static_assert(kSpectrumBands == dsp::SpectrumAnalyser::kBands, "a snapshot holds exactly the analyser's bands");

void foldReadings(HeldLevels &held,
                  std::span<const rt::MeterReading> readings,
                  std::uint64_t nanos,
                  std::uint64_t holdNanos,
                  BlockTelemetry &into)
{
    const std::size_t channels = std::min(readings.size(), rt::kMaxMeterChannels);
    // Whether anything at all was measured this time. It is a question
    // about the block and not about one channel: a cycle fills every
    // channel of it or none, and a silent right-hand side is a fact about
    // the sound rather than about the timing.
    bool measured = false;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        const rt::MeterReading &reading = readings[channel];
        if (reading.frames > 0) {
            measured = true;
            held.peak[channel] = reading.peak;
            held.rms[channel] = reading.rms;
            held.measuredChannels |= 1U << channel;
        }
        // A clip is news when it happens and never carried forward: the
        // latch that keeps the light on belongs to whoever is watching, and
        // repeating it here would light it again for ever.
        into.clipped = into.clipped || reading.clipped;
    }
    if (measured) {
        held.measuredAt = nanos;
    }
    // Nothing measured and nothing measured recently enough is a block
    // whose node has stopped being scheduled, and then silence is true.
    const bool stale = held.measuredAt == 0 || nanos - held.measuredAt > holdNanos;
    if (stale) {
        held.peak.fill(0.0f);
        held.rms.fill(0.0f);
        held.measuredChannels = 0;
    }
    // **Not until every channel has something to say**. The audio
    // thread measures a block's channels one after another and this reads
    // them one after another, so the first tick after a block starts — or
    // starts again — can find the right-hand side measured and the left
    // not yet. Published then, the block would be active with a left side
    // at nothing: a meter that jumps up from the floor on one side, and a
    // level that says silence under a light that says sound.
    const std::uint32_t every = channels >= 32 ? ~0U : (1U << channels) - 1U;
    const bool complete = (held.measuredChannels & every) == every;
    for (std::size_t channel = 0; channel < channels; ++channel) {
        into.peak[channel] = complete ? held.peak[channel] : 0.0f;
        into.rms[channel] = complete ? held.rms[channel] : 0.0f;
        into.active = into.active || into.peak[channel] >= rt::MeterReading::kActivity;
    }
}

class BlockAnalysis::Private
{
public:
    rt::BlockMeter *meter = nullptr;
    int channel = kBothChannels;
    dsp::SpectrumAnalyser output;
    dsp::SpectrumAnalyser input;
    /// Whether anything has come through the input ring since the block
    /// was followed: a block with no inputs has no spectrum before it, and
    /// saying it has one of silence would draw a curve that is not there.
    bool inputHeard = false;
    /// How many band values the block had published when it was followed.
    /// Values published before then are from an earlier look.
    std::uint64_t bandsFrom = 0;
    /// Where a drained stretch of a ring lands, interleaved, and the same
    /// taken down to one channel. Sized once.
    std::vector<float> drained = std::vector<float>(8192 * rt::BlockMeter::kAnalysisChannels);
    std::vector<float> mono = std::vector<float>(8192);

    void restart(double sampleRate)
    {
        output.prepare(sampleRate);
        input.prepare(sampleRate);
        inputHeard = false;
    }

    /// Everything \a tap holds, into \a analyser, as the channel asked
    /// for. Returns whether anything was there.
    bool drain(rt::AudioTap &tap, dsp::SpectrumAnalyser &analyser)
    {
        const std::size_t channels = rt::BlockMeter::kAnalysisChannels;
        bool any = false;
        while (tap.readableFrames() > 0) {
            const std::size_t frames = tap.read(drained, mono.size());
            if (frames == 0) {
                break;
            }
            any = true;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const float first = drained[frame * channels];
                const float second = drained[frame * channels + 1];
                // Both sides mixed to one, as a person hears them together,
                // or the one side a block for one ear works on.
                mono[frame] = channel == 0 ? first : channel == 1 ? second : 0.5f * (first + second);
            }
            analyser.push(std::span<const float>(mono.data(), frames));
        }
        return any;
    }
};

BlockAnalysis::BlockAnalysis()
    : d(std::make_unique<Private>())
{
}

BlockAnalysis::~BlockAnalysis() = default;

void BlockAnalysis::follow(rt::BlockMeter *meter, double sampleRate)
{
    d->meter = meter;
    if (meter == nullptr) {
        return;
    }
    // What is in the rings from an earlier look is old news.
    for (rt::AudioTap *tap : {meter->analysis(), meter->inputAnalysis()}) {
        if (tap != nullptr) {
            tap->discard(tap->readableFrames());
        }
    }
    d->bandsFrom = meter->bandPublishCount();
    d->restart(sampleRate > 0.0 ? sampleRate : 48000.0);
}

rt::BlockMeter *BlockAnalysis::following() const noexcept
{
    return d->meter;
}

void BlockAnalysis::setChannel(int channel)
{
    const int clamped = channel == 0 || channel == 1 ? channel : kBothChannels;
    if (clamped == d->channel) {
        return;
    }
    d->channel = clamped;
    // What both averages hold is of the other channel.
    d->restart(d->output.sampleRate() > 0.0 ? d->output.sampleRate() : 48000.0);
}

int BlockAnalysis::channel() const noexcept
{
    return d->channel;
}

void BlockAnalysis::take(double sampleRate, TelemetrySnapshot &snapshot)
{
    rt::BlockMeter *meter = d->meter;
    if (meter == nullptr || meter->analysis() == nullptr) {
        return;
    }
    if (sampleRate > 0.0 && sampleRate != d->output.sampleRate()) {
        d->restart(sampleRate);
    }
    (void)d->drain(*meter->analysis(), d->output);
    d->inputHeard = d->drain(*meter->inputAnalysis(), d->input) || d->inputHeard;

    snapshot.analysedChannel = d->channel;
    snapshot.spectrumBands = static_cast<std::uint32_t>(kSpectrumBands);
    std::ranges::copy(d->output.bandsDb(), std::begin(snapshot.spectrum));
    if (d->inputHeard) {
        snapshot.inputSpectrumBands = static_cast<std::uint32_t>(kSpectrumBands);
        std::ranges::copy(d->input.bandsDb(), std::begin(snapshot.inputSpectrum));
    }
    if (meter->bandPublishCount() > d->bandsFrom) {
        const rt::BandValues bands = meter->bands();
        snapshot.bandValueCount = std::min<std::uint32_t>(bands.count, rt::kMaxBandValues);
        std::copy_n(std::begin(bands.values), snapshot.bandValueCount, std::begin(snapshot.bandValues));
    }
}

class TelemetrySampler::Private
{
public:
    SessionRuntime *runtime = nullptr;
    /// **Where the snapshots go: shared memory** (ADR-0018), so that a
    /// window in another process maps the very slot a window in this one
    /// holds a pointer to.
    PublishedSlot published;
    TelemetrySlot &slot = published.slot();
    QTimer timer;
    std::uint64_t frame = 0;
    /// By block id, because an edit reorders them and a slot number would
    /// hand one block's levels to another.
    std::map<QString, HeldLevels> held;
    XrunHistory xruns;

    /// The block whose spectra are asked for, and of which channel.
    QString analysed;
    int analysedChannel = kBothChannels;
    /// The meter they are being taken from now — compared, never
    /// dereferenced, unless the runtime still lists it.
    rt::BlockMeter *analysing = nullptr;
    BlockAnalysis analysis;

    /// Stops copying out whatever is being copied, if the runtime still
    /// has it.
    void stopAnalysing(const std::vector<SessionRuntime::MeteredBlock> &metered)
    {
        if (analysing == nullptr) {
            return;
        }
        for (const SessionRuntime::MeteredBlock &block : metered) {
            if (block.meter == analysing) {
                block.meter->setAnalysing(false);
            }
        }
        analysing = nullptr;
        analysis.follow(nullptr, 0.0);
    }

    /// Follows the block asked for, drains what its input and output left
    /// in the rings, and puts its spectra and band values in \a snapshot.
    void takeSpectrum(const std::vector<SessionRuntime::MeteredBlock> &metered,
                      std::uint32_t sampleRate,
                      TelemetrySnapshot &snapshot)
    {
        rt::BlockMeter *meter = nullptr;
        if (!analysed.isEmpty()) {
            for (const SessionRuntime::MeteredBlock &block : metered) {
                if (block.id == analysed) {
                    meter = block.meter;
                }
            }
        }
        const double rate = sampleRate > 0 ? sampleRate : 48000.0;
        if (meter != analysing) {
            stopAnalysing(metered);
            if (meter != nullptr) {
                meter->setAnalysing(true);
                analysis.follow(meter, rate);
                analysing = meter;
            }
        }
        if (analysing == nullptr) {
            return;
        }
        analysis.setChannel(analysedChannel);
        analysis.take(rate, snapshot);
        writeId(snapshot.analysedId, analysed);
    }

    /// How long a level may be carried forward before the session is taken
    /// to have gone quiet for real.
    ///
    /// A cycle is a quantum long, so nothing new inside one quantum is
    /// ordinary and says nothing; past that, plus the tick that would have
    /// caught it, the node has stopped being scheduled and silence is the
    /// truth. The quantum is whatever the node last ran at, and a node that
    /// has not said yet is given the longest one the engine allows, because
    /// guessing short is the flicker all over again.
    [[nodiscard]] std::uint64_t holdNanos(std::uint32_t frames, std::uint32_t sampleRate) const
    {
        constexpr std::uint64_t kNanosPerSecond = 1'000'000'000;
        const std::uint32_t quantum = frames > 0 ? frames : dsp::kMaxBlockFrames;
        const std::uint32_t rate = sampleRate > 0 ? sampleRate : 48000U;
        const auto period = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(TelemetrySampler::kPeriod).count());
        return static_cast<std::uint64_t>(quantum) * kNanosPerSecond / rate + period;
    }
};

TelemetrySampler::TelemetrySampler(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->timer.setInterval(kPeriod);
    // **Coarse timers are not good enough here.** A meter that arrives when
    // the event loop gets round to it is a meter that stutters, and Qt's
    // coarse timer is allowed to be 5% late, which at 16 ms is most of a
    // frame.
    d->timer.setTimerType(Qt::PreciseTimer);
    connect(&d->timer, &QTimer::timeout, this, &TelemetrySampler::sample);
}

TelemetrySampler::~TelemetrySampler() = default;

void TelemetrySampler::watch(SessionRuntime *runtime)
{
    // **Stop copying out of the runtime being left**, while it can still be
    // asked which meters it has.
    if (d->runtime != nullptr && runtime != d->runtime) {
        d->stopAnalysing(d->runtime->meters());
    }
    d->analysing = nullptr;
    d->analysis.follow(nullptr, 0.0);
    if (runtime != d->runtime) {
        // Another runtime's nodes count their late cycles from nought.
        d->xruns.restart();
    }
    d->runtime = runtime;
}

void TelemetrySampler::analyse(const QString &blockId, int channel)
{
    d->analysed = blockId;
    d->analysedChannel = channel;
}

QString TelemetrySampler::analysed() const
{
    return d->analysed;
}

int TelemetrySampler::analysedChannel() const
{
    return d->analysedChannel;
}

void TelemetrySampler::start()
{
    d->timer.start();
}

void TelemetrySampler::stop()
{
    d->timer.stop();
    // One last snapshot, so that a meter left on the screen goes to nothing
    // rather than staying where the music stopped.
    sample();
}

bool TelemetrySampler::isRunning() const
{
    return d->timer.isActive();
}

const TelemetrySlot &TelemetrySampler::slot() const
{
    return d->slot;
}

int TelemetrySampler::shareReadOnly() const
{
    return d->published.shareReadOnly();
}

void TelemetrySampler::sample()
{
    TelemetrySnapshot snapshot;
    snapshot.frame = ++d->frame;
    snapshot.nanos = rt::monotonicNanos();

    SessionRuntime *runtime = d->runtime;
    if (runtime == nullptr || !runtime->isRunning()) {
        // Nothing running is a snapshot of nothing, not the last one left
        // where it was.
        d->slot.store(snapshot);
        return;
    }

    snapshot.running = true;
    // **A session cut around its plugins is several nodes** (ADR-0014),
    // run one after another in the same cycle: their loads add up, and a
    // cycle one of them missed is a cycle the session missed.
    const std::vector<pw::SessionNode *> nodes = runtime->nodes();
    for (const pw::SessionNode *node : nodes) {
        const pw::SessionTelemetry node_telemetry = node->telemetry();
        snapshot.dspLoad += node_telemetry.lastLoad;
        snapshot.peakDspLoad += node_telemetry.peakLoad;
        snapshot.xruns = static_cast<std::uint32_t>(std::max<std::uint64_t>(snapshot.xruns, node_telemetry.xruns));
        if (node == nodes.front()) {
            snapshot.sampleRate = node_telemetry.sampleRate;
            snapshot.frames = node_telemetry.frames;
        }
    }
    snapshot.processLatency = runtime->latencySamples();
    d->xruns.observe(snapshot.xruns, QDateTime::currentMSecsSinceEpoch());

    const std::uint64_t hold = d->holdNanos(snapshot.frames, snapshot.sampleRate);
    for (std::pair<const QString, HeldLevels> &entry : d->held) {
        entry.second.seen = false;
    }

    const std::vector<SessionRuntime::MeteredBlock> metered = runtime->meters();
    for (const SessionRuntime::MeteredBlock &block : metered) {
        if (snapshot.blockCount >= kMaxMeteredBlocks || block.meter == nullptr) {
            break;
        }
        BlockTelemetry &into = snapshot.blocks[snapshot.blockCount];
        writeId(into.id, block.id);
        into.enabled = block.enabled;
        into.latencySamples = block.latencySamples;
        HeldLevels &last = d->held[block.id];
        last.seen = true;
        const std::size_t channels = std::min(block.meter->channelCount(), rt::kMaxMeterChannels);
        into.channelCount = static_cast<std::uint32_t>(channels);
        std::array<rt::MeterReading, rt::kMaxMeterChannels> readings{};
        for (std::size_t channel = 0; channel < channels; ++channel) {
            // **Taking empties it**, which is why nothing else may read
            // these: a second reader would see the silence the first one
            // left behind.
            readings[channel] = block.meter->channel(channel).take();
        }
        foldReadings(last, std::span<const rt::MeterReading>(readings.data(), channels), snapshot.nanos, hold, into);
        for (std::size_t value = 0; value < rt::kMaxMeterValues; ++value) {
            into.values[value] = block.meter->value(value);
        }
        ++snapshot.blockCount;
    }

    std::erase_if(d->held, [](const std::pair<const QString, HeldLevels> &entry) { return !entry.second.seen; });

    d->takeSpectrum(metered, snapshot.sampleRate, snapshot);

    d->slot.store(snapshot);
}

std::vector<XrunEvent> TelemetrySampler::xrunHistory() const
{
    return d->xruns.events();
}

} // namespace rb::core

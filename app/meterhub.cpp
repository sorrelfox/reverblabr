// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "meterhub.h"

#include "dsp/decibel.h"

#include <QElapsedTimer>

#include <algorithm>
#include <utility>

namespace rb::ui
{

namespace
{

[[nodiscard]] qint64 sinceTheProcessStarted()
{
    static const QElapsedTimer running = [] {
        QElapsedTimer timer;
        timer.start();
        return timer;
    }();
    return running.elapsed();
}

} // namespace

class MeterHub::Private
{
public:
    const core::TelemetrySlot *slot = nullptr;
    QList<BlockLevels> blocks;
    /// Where each block sits in the list, so that a face can ask for its own
    /// levels by id without walking fifty of them.
    QHash<QString, qsizetype> byId;
    /// When each channel's hold was last pushed up, and when each block last
    /// clipped, in the hub's own milliseconds.
    QList<std::array<qint64, rt::kMaxMeterChannels>> heldAt;
    QList<qint64> clippedAt;
    /// And when each channel last did, so that a dot can be lit per side.
    QList<std::array<qint64, rt::kMaxMeterChannels>> clippedChannelsAt;

    std::function<qint64()> clock = &sinceTheProcessStarted;

    /// How long a peak line waits and how fast it comes down afterwards.
    /// The two travel together because "forever" is a fall rate of zero.
    struct Timing {
        qint64 holdMs = kHoldMs;
        float decayDbPerSecond = kDecayDbPerSecond;
    };
    /// What a block has been told about itself, keyed by its id rather than
    /// by where it sits in the snapshot: a block keeps its hold across an
    /// edit that moves it, and can be told about before it has been heard
    /// from at all.
    QHash<QString, Timing> perBlock;
    /// And what a block nobody has spoken for uses.
    Timing window;

    [[nodiscard]] Timing timingOf(const QString &blockId) const
    {
        const auto found = perBlock.constFind(blockId);
        return found == perBlock.constEnd() ? window : *found;
    }

    core::TelemetrySnapshot latest;
    std::uint64_t lastFrame = 0;
    qint64 lastTick = 0;

    /// Grows the lists to match a snapshot, keeping what is already there:
    /// a block that was in the last snapshot keeps its hold.
    void resize(std::uint32_t count)
    {
        const auto wanted = static_cast<qsizetype>(count);
        while (blocks.size() > wanted) {
            const QString gone = blocks.back().id;
            blocks.removeLast();
            heldAt.removeLast();
            clippedAt.removeLast();
            clippedChannelsAt.removeLast();
            byId.remove(gone);
        }
        while (blocks.size() < wanted) {
            blocks.append(BlockLevels{});
            heldAt.append(std::array<qint64, rt::kMaxMeterChannels>{});
            clippedAt.append(0);
            clippedChannelsAt.append(std::array<qint64, rt::kMaxMeterChannels>{});
        }
    }
};

MeterHub::MeterHub(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
}

MeterHub::~MeterHub() = default;

void MeterHub::watch(const core::TelemetrySlot *slot)
{
    d->slot = slot;
    d->lastFrame = 0;
    d->resize(0);
    // **The holds go with the session they were chosen in.** Block ids are
    // only unique inside a session, so keeping them would give a block of
    // the next session a wait somebody chose for a different meter that
    // happened to share its name.
    d->perBlock.clear();
    d->latest = core::TelemetrySnapshot{};
}

void MeterHub::setClock(std::function<qint64()> clock)
{
    d->clock = clock ? std::move(clock) : std::function<qint64()>(&sinceTheProcessStarted);
}

namespace
{

/// Whether \a latest carries \a blockId's analysis at all.
[[nodiscard]] bool analysing(const core::TelemetrySnapshot &latest, const QString &blockId)
{
    return latest.running && !blockId.isEmpty()
        && QString::fromUtf8(static_cast<const char *>(latest.analysedId)) == blockId;
}

} // namespace

std::span<const float> MeterHub::spectrum(const QString &blockId) const
{
    const core::TelemetrySnapshot &latest = d->latest;
    if (!analysing(latest, blockId)) {
        return {};
    }
    return std::span<const float>(latest.spectrum, std::min<std::size_t>(latest.spectrumBands, core::kSpectrumBands));
}

std::span<const float> MeterHub::inputSpectrum(const QString &blockId) const
{
    const core::TelemetrySnapshot &latest = d->latest;
    if (!analysing(latest, blockId)) {
        return {};
    }
    return std::span<const float>(latest.inputSpectrum,
                                  std::min<std::size_t>(latest.inputSpectrumBands, core::kSpectrumBands));
}

std::span<const float> MeterHub::bandValues(const QString &blockId) const
{
    const core::TelemetrySnapshot &latest = d->latest;
    if (!analysing(latest, blockId)) {
        return {};
    }
    return std::span<const float>(latest.bandValues, std::min<std::size_t>(latest.bandValueCount, rt::kMaxBandValues));
}

bool MeterHub::tick()
{
    if (d->slot == nullptr) {
        return false;
    }
    const core::TelemetrySnapshot snapshot = d->slot->load();
    const qint64 now = d->clock();
    // **A snapshot that has not changed is still a frame.** The holds fall
    // in wall-clock time, so a session that has stopped publishing still has
    // meters that come down rather than meters that stick.
    const bool fresh = snapshot.frame != d->lastFrame;
    const qint64 sinceLast = d->lastTick == 0 ? 0 : now - d->lastTick;
    d->lastTick = now;
    d->lastFrame = snapshot.frame;
    d->latest = snapshot;

    d->resize(snapshot.blockCount);
    for (qsizetype index = 0; index < d->blocks.size(); ++index) {
        const core::BlockTelemetry &from = snapshot.blocks[index];
        BlockLevels &into = d->blocks[index];
        const QString id = QString::fromUtf8(from.id);
        if (into.id != id) {
            d->byId.remove(into.id);
            into = BlockLevels{};
            into.id = id;
            d->heldAt[index] = {};
            d->clippedAt[index] = 0;
            d->clippedChannelsAt[index] = {};
            d->byId.insert(id, index);
        }
        into.channelCount = std::min<std::uint32_t>(from.channelCount, rt::kMaxMeterChannels);
        into.enabled = from.enabled;
        into.active = from.active;
        into.latencyMs = snapshot.sampleRate > 0
            ? 1000.0f * static_cast<float>(from.latencySamples) / static_cast<float>(snapshot.sampleRate)
            : 0.0f;
        into.values = std::to_array(from.values);

        // This block's own wait and fall, not the window's: two meters in
        // one session hold for the times they were each given.
        const Private::Timing timing = d->timingOf(into.id);
        for (std::uint32_t channel = 0; channel < into.channelCount; ++channel) {
            const float peakDb = dsp::gainToDb(from.peak[channel]);
            into.peakDb[channel] = peakDb;
            // **Full scale on this side, not on the block.** The snapshot
            // carries one clip flag for the whole block and a peak for each
            // channel, and the peak is what the flag was made from — so the
            // side that did it can be worked out here rather than guessed
            // at in the panel.
            if (from.peak[channel] >= 1.0f) {
                d->clippedChannelsAt[index][channel] = now;
            }
            into.rmsDb[channel] = dsp::gainToDb(from.rms[channel]);

            float held = into.holdDb[channel];
            if (peakDb >= held) {
                held = peakDb;
                d->heldAt[index][channel] = now;
            } else if (now - d->heldAt[index][channel] > timing.holdMs) {
                // Falling at a steady number of decibels a second is what a
                // person reads as a meter coming down; falling by a fraction
                // per frame would fall at a different speed on every screen.
                held -= timing.decayDbPerSecond * static_cast<float>(sinceLast) / 1000.0f;
                held = std::max(held, peakDb);
            }
            into.holdDb[channel] = std::max(held, dsp::kSilenceDb);
        }
        for (std::uint32_t channel = into.channelCount; channel < rt::kMaxMeterChannels; ++channel) {
            into.peakDb[channel] = dsp::kSilenceDb;
            into.rmsDb[channel] = dsp::kSilenceDb;
            into.holdDb[channel] = dsp::kSilenceDb;
        }

        if (from.clipped) {
            d->clippedAt[index] = now;
        }
        into.clipped = d->clippedAt[index] != 0 && now - d->clippedAt[index] <= kClipHoldMs;
        for (std::size_t channel = 0; channel < rt::kMaxMeterChannels; ++channel) {
            const qint64 lit = d->clippedChannelsAt[index][channel];
            into.clippedChannels[channel] = lit != 0 && now - lit <= kClipHoldMs;
        }
    }

    if (fresh) {
        Q_EMIT updated();
    }
    return fresh;
}

const BlockLevels *MeterHub::block(const QString &id) const
{
    const auto found = d->byId.constFind(id);
    if (found == d->byId.constEnd() || *found >= d->blocks.size()) {
        return nullptr;
    }
    return &d->blocks.at(*found);
}

const QList<BlockLevels> &MeterHub::blocks() const
{
    return d->blocks;
}

float MeterHub::dspLoad() const
{
    return d->latest.dspLoad;
}

float MeterHub::peakDspLoad() const
{
    return d->latest.peakDspLoad;
}

std::uint32_t MeterHub::xruns() const
{
    return d->latest.xruns;
}

std::uint32_t MeterHub::sampleRate() const
{
    return d->latest.sampleRate;
}

std::uint32_t MeterHub::frames() const
{
    return d->latest.frames;
}

bool MeterHub::isRunning() const
{
    return d->latest.running;
}

std::uint64_t MeterHub::frame() const
{
    return d->latest.frame;
}

void MeterHub::setHold(const QString &blockId, qint64 milliseconds)
{
    if (blockId.isEmpty()) {
        // A caller that has not been told which block it means yet. Taking
        // it as the window's setting would give every meter a wait chosen
        // for none of them.
        return;
    }
    Private::Timing &timing = d->perBlock[blockId];
    timing.holdMs = std::max<qint64>(0, milliseconds);
}

void MeterHub::setDecay(const QString &blockId, float dbPerSecond)
{
    if (blockId.isEmpty()) {
        return;
    }
    Private::Timing &timing = d->perBlock[blockId];
    // Zero would be a hold that never falls, which is what "forever" means
    // on the Peak/RMS block; negative would be a line that climbs on its
    // own, which is nothing.
    timing.decayDbPerSecond = std::max(0.0f, dbPerSecond);
}

void MeterHub::setHold(qint64 milliseconds)
{
    d->window.holdMs = std::max<qint64>(0, milliseconds);
}

void MeterHub::setDecay(float dbPerSecond)
{
    d->window.decayDbPerSecond = std::max(0.0f, dbPerSecond);
}

qint64 MeterHub::holdMs(const QString &blockId) const
{
    return d->timingOf(blockId).holdMs;
}

float MeterHub::decayDbPerSecond(const QString &blockId) const
{
    return d->timingOf(blockId).decayDbPerSecond;
}

void MeterHub::clearClip(const QString &blockId)
{
    if (blockId.isEmpty()) {
        for (qsizetype index = 0; index < d->clippedAt.size(); ++index) {
            d->clippedAt[index] = 0;
            d->clippedChannelsAt[index] = {};
            d->blocks[index].clipped = false;
            d->blocks[index].clippedChannels = {};
        }
        Q_EMIT updated();
        return;
    }
    const auto found = d->byId.constFind(blockId);
    if (found == d->byId.constEnd()) {
        return;
    }
    d->clippedAt[*found] = 0;
    d->clippedChannelsAt[*found] = {};
    d->blocks[*found].clipped = false;
    d->blocks[*found].clippedChannels = {};
    Q_EMIT updated();
}

} // namespace rb::ui

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/telemetry.h"
#include "dsp/decibel.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

namespace rb::ui
{

/// Channels that all read as silence, which is what a meter shows before it
/// has heard anything. **Not zero**: zero decibels is full scale, and a
/// meter that started there would open every session pinned at the top.
[[nodiscard]] constexpr std::array<float, rt::kMaxMeterChannels> silentChannels()
{
    std::array<float, rt::kMaxMeterChannels> channels{};
    channels.fill(dsp::kSilenceDb);
    return channels;
}

/// One block's levels as a meter draws them.
///
/// **Decibels here, amplitudes in the snapshot.** The engine publishes what
/// it measured; turning that into something with a scale is the interface's
/// business, and so is how long a peak stays up.
struct BlockLevels {
    QString id;
    std::uint32_t channelCount = 0;
    /// The loudest sample since the last frame, in dBFS. kSilenceDb when
    /// there was nothing.
    std::array<float, rt::kMaxMeterChannels> peakDb = silentChannels();
    /// The peak a meter's thin line sits at: it jumps up at once, waits, and
    /// then falls at a steady number of decibels a second.
    std::array<float, rt::kMaxMeterChannels> holdDb = silentChannels();
    /// The root mean square since the last frame, in dBFS: the bar itself.
    std::array<float, rt::kMaxMeterChannels> rmsDb = silentChannels();
    /// What the block publishes about itself — a gain reduction, an input
    /// level — exactly as it published it.
    std::array<float, rt::kMaxMeterValues> values{};
    /// Whether the block reached full scale lately. Latched for kClipHold so
    /// that a single sample is something a person can see.
    bool clipped = false;
    /// The same, one side at a time. **A dot per channel**, because a meter
    /// that only says "something clipped" leaves a person guessing which of
    /// the two sides it was, which is the first thing they want to know.
    std::array<bool, rt::kMaxMeterChannels> clippedChannels{};
    bool active = false;
    bool enabled = true;
    /// How late the block makes the sound, in milliseconds.
    float latencyMs = 0.0f;
};

/// Where the interface reads its meters.
///
/// **One read per frame for the whole session, not one per meter.** Fifty
/// meters on the screen are fifty views of one snapshot; asking the engine
/// fifty times would be fifty round trips and fifty chances to see a
/// different moment in each.
///
/// The hub owns the part of a meter that is about time rather than about
/// sound: how long a peak stays up, how fast it falls, and how long a clip
/// light stays on. All of it is in wall-clock time, because that is what a
/// person sees; none of it is in frames, which would run at a different
/// speed at every quantum.
///
/// **The wait and the fall are a block's, not the window's**.
/// `peakHold` is a parameter of the Peak/RMS block, so every meter in a
/// session carries its own; one clock for the whole hub meant that whichever
/// panel was touched last decided for all of them, and two meters at
/// different points in a lane — one watching the input, one the output — are
/// exactly where different holds earn their keep. So both are kept per
/// block, keyed the way block() already keys the levels.
///
/// **The fall rate follows the hold rather than staying window-wide.**
/// Nothing yet asks for a fall rate of its own, so leaving it as one setting
/// was the other option — but "forever" is already spelled as a decay of
/// zero rather than as a wait long enough to pass for never, which makes the
/// fall rate part of the choice whether it wants to be or not. Window-wide,
/// one meter set to "Until reset" would freeze the line of every other meter
/// in the session: the same mix-up, moved. A hold from one block and a fall
/// rate from another would be a line nobody could explain, so the two travel
/// together.
///
/// Making `peakHold` a window setting instead would have been honest and
/// smaller, and it is rejected on purpose: the choice belongs to the block a
/// person is looking at, and taking it away to fit the hub would be the
/// interface apologising for its plumbing.
class MeterHub : public QObject
{
    Q_OBJECT

public:
    /// How long the peak line holds before it starts to fall.
    static constexpr qint64 kHoldMs = 1200;
    /// How fast it falls after that.
    static constexpr float kDecayDbPerSecond = 20.0f;
    /// How long the clip light stays on after the last sample that lit it.
    static constexpr qint64 kClipHoldMs = 1500;

    explicit MeterHub(QObject *parent = nullptr);
    ~MeterHub() override;

    /// Reads \a slot from now on, or nothing when it is null. The slot must
    /// outlive the hub or be taken away with another call.
    void watch(const core::TelemetrySlot *slot);

    /// Takes the latest snapshot and ages the holds. **Called on the frame
    /// tick**, whatever rate the screen runs at: reading the same snapshot
    /// twice costs nothing and misses nothing, and so does missing one.
    ///
    /// Returns whether the snapshot was a new one.
    bool tick();

    /// The levels of one block, or null if the snapshot has nothing about
    /// it. The pointer is good until the next tick().
    [[nodiscard]] const BlockLevels *block(const QString &id) const;
    /// \a blockId's live spectrum, core::kSpectrumBands levels in dB
    /// low to high, or nothing when the snapshot is not carrying that
    /// block's. Good until the next tick().
    [[nodiscard]] std::span<const float> spectrum(const QString &blockId) const;
    /// The same for the spectrum **before** the block, or nothing
    /// for a block with no inputs.
    [[nodiscard]] std::span<const float> inputSpectrum(const QString &blockId) const;
    /// \a blockId's band values, as it last published them, or
    /// nothing. Good until the next tick().
    [[nodiscard]] std::span<const float> bandValues(const QString &blockId) const;
    /// Every block in the snapshot, in the order the engine published them.
    [[nodiscard]] const QList<BlockLevels> &blocks() const;

    /// What the status bar shows.
    [[nodiscard]] float dspLoad() const;
    [[nodiscard]] float peakDspLoad() const;
    [[nodiscard]] std::uint32_t xruns() const;
    [[nodiscard]] std::uint32_t sampleRate() const;
    [[nodiscard]] std::uint32_t frames() const;
    [[nodiscard]] bool isRunning() const;
    /// The number of the snapshot last read, which counts up in the engine.
    [[nodiscard]] std::uint64_t frame() const;

    /// The clock the holds and the decay are measured against, in
    /// milliseconds. A test sets its own so that a second of decay costs it
    /// nothing.
    void setClock(std::function<qint64()> clock);

    /// How long one block's peak line holds before it starts to fall, and
    /// how fast it falls afterwards. **Both in wall-clock time**, so they
    /// look the same on a 60 Hz screen and a 144 Hz one.
    void setHold(const QString &blockId, qint64 milliseconds);
    void setDecay(const QString &blockId, float dbPerSecond);

    /// The same for every block that has not been told otherwise. **A
    /// default, not an override**: a block that has been given a hold of its
    /// own keeps it.
    void setHold(qint64 milliseconds);
    void setDecay(float dbPerSecond);

    /// What a block waits and how fast it falls — its own settings, or the
    /// window's when it has none of its own.
    [[nodiscard]] qint64 holdMs(const QString &blockId) const;
    [[nodiscard]] float decayDbPerSecond(const QString &blockId) const;

    /// Puts out the clip light of one block, or of every block. **What
    /// clicking a meter does**: a latch nobody can clear is a light that is
    /// on for the rest of the session.
    void clearClip(const QString &blockId = {});

Q_SIGNALS:
    /// A new snapshot has been read. The interface redraws what it must.
    void updated();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui

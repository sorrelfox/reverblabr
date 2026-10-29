// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "meterhub.h"

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

#include <memory>

namespace rb::ui
{

/// Where every meter in the window reads its levels.
///
/// **One read per frame for the whole session, not one per meter.** Fifty
/// meters on the screen are fifty views of one snapshot; asking the engine
/// fifty times would be fifty round trips and fifty chances to see a
/// different moment in each. The hold and the decay are worked out once
/// here, in wall-clock time, so they look the same on a 60 Hz screen and a
/// 144 Hz one.
class Meters : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Counts up once per frame that brought something new. **What a meter
    /// binds to**: one property for the whole window rather than a signal
    /// per block.
    Q_PROPERTY(qulonglong frame READ frame NOTIFY ticked)
    /// Whether anything is being published at all. False is the idle state
    /// every meter shows: a stopped session is not a quiet one.
    Q_PROPERTY(bool live READ isLive NOTIFY ticked)

public:
    /// How often the levels are read. Sixty a second is what the engine
    /// publishes at; reading the same snapshot twice costs nothing and
    /// misses nothing, and so does missing one.
    static constexpr int kTickMs = 16;

    explicit Meters(QObject *parent = nullptr);
    ~Meters() override;

    [[nodiscard]] qulonglong frame() const;
    [[nodiscard]] bool isLive() const;

    /// Follows the session's telemetry. Empty stops reading, which is what
    /// closing a session does.
    Q_INVOKABLE void watch(const QString &sessionId);

    /// One block's levels, as a meter draws them: channelCount, rmsDb,
    /// peakDb, holdDb, values, clipped, clippedChannels, active. Empty for a
    /// block the snapshot has nothing about, which is a block that is not
    /// running.
    ///
    /// **`values` is whatever the block publishes about itself** — a gain
    /// reduction, whether its device is there — exactly as it published it.
    /// What each one means is the block's business and its panel's.
    [[nodiscard]] Q_INVOKABLE QVariantMap levels(const QString &blockId) const;

    /// **Asks for \a blockId's live spectra and band values**,
    /// or nobody's when it is empty; of both sides mixed, or of \a channel
    /// (0 or 1) alone, for a block that works on one ear. What a curve
    /// editor says when it opens and when it closes: the engine copies a
    /// block's audio out only while somebody asks.
    Q_INVOKABLE void analyse(const QString &blockId, int channel = -1);
    /// The block whose spectrum was last asked for, and of which channel.
    [[nodiscard]] Q_INVOKABLE QString analysed() const;
    [[nodiscard]] Q_INVOKABLE int analysedChannel() const;

    /// \a blockId's live spectrum, one level in dB a band, low to high; or
    /// an empty list when the session is not publishing that block's.
    [[nodiscard]] Q_INVOKABLE QVariantList spectrum(const QString &blockId) const;
    /// The same before the block: what goes into it. Empty for a
    /// block with no inputs.
    [[nodiscard]] Q_INVOKABLE QVariantList inputSpectrum(const QString &blockId) const;
    /// What the block says about each of its bands, such as the gain
    /// it applies in each, as it last published them; or an empty list.
    [[nodiscard]] Q_INVOKABLE QVariantList bandValues(const QString &blockId) const;
    /// Where each of the spectrum's bands is centred, in Hz.
    [[nodiscard]] Q_INVOKABLE QVariantList spectrumBandsHz() const;

    /// Puts out a clip light. **What clicking a meter does**: a latch
    /// nobody can clear is a light that is on for the rest of the session.
    Q_INVOKABLE void resetClip(const QString &blockId);

    /// How long one block's peak line holds and how fast it falls
    /// afterwards. **Per block, because `peakHold` is**: every
    /// Peak/RMS block in a session carries its own, and a hold for the whole
    /// window would be whichever panel was touched last.
    ///
    /// **Said by BlockModel, which follows the session**, and by
    /// nothing on the screen. A hub told by a panel or a face only knows
    /// about the blocks somebody has looked at.
    Q_INVOKABLE void setHold(const QString &blockId, int milliseconds);
    Q_INVOKABLE void setDecay(const QString &blockId, qreal dbPerSecond);

    /// What a block is waiting and falling at now. **How a test sees which
    /// block a panel spoke for**, which is the one thing about a per-block
    /// setting that cannot be read off either side on its own.
    [[nodiscard]] Q_INVOKABLE int holdMs(const QString &blockId) const;
    [[nodiscard]] Q_INVOKABLE qreal decayDbPerSecond(const QString &blockId) const;

    /// The hub itself, for a test and for anything in C++ that needs the
    /// levels without going through a QVariantMap.
    [[nodiscard]] MeterHub *hub() const;

Q_SIGNALS:
    void ticked();
    /// A different session's levels are being read from now on.
    ///
    /// **Every block's hold went with the session it was chosen in**, so
    /// the hub is now back to its default for all of them and whatever
    /// speaks for the blocks has to speak again. Without this, whether a
    /// meter held for its own setting would come down to whether the
    /// session's blocks arrived before or after watch() — which is the sort
    /// of order nothing should depend on.
    void watching();

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::ui

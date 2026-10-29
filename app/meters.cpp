// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "meters.h"

#include "appshell.h"
#include "core/enginefacade.h"

#include <QTimer>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

[[nodiscard]] QList<qreal> asList(const std::array<float, rt::kMaxMeterChannels> &values, std::uint32_t count)
{
    QList<qreal> list;
    list.reserve(static_cast<qsizetype>(count));
    for (std::uint32_t channel = 0; channel < count && channel < values.size(); ++channel) {
        list.append(static_cast<qreal>(values[channel]));
    }
    return list;
}

[[nodiscard]] QList<bool> asFlags(const std::array<bool, rt::kMaxMeterChannels> &flags, std::uint32_t count)
{
    QList<bool> list;
    list.reserve(static_cast<qsizetype>(count));
    for (std::uint32_t channel = 0; channel < count && channel < flags.size(); ++channel) {
        list.append(flags[channel]);
    }
    return list;
}

/// What a block publishes about itself, all of it: which value means what
/// is the block's business and its face's.
[[nodiscard]] QList<qreal> asValues(const std::array<float, rt::kMaxMeterValues> &values)
{
    QList<qreal> list;
    list.reserve(static_cast<qsizetype>(values.size()));
    for (const float value : values) {
        list.append(static_cast<qreal>(value));
    }
    return list;
}

} // namespace

class Meters::Private
{
public:
    MeterHub hub;
    QTimer ticker;
    qulonglong frame = 0;
    bool live = false;
    QString sessionId;
    QString analysed;
    int analysedChannel = core::kBothChannels;
};

Meters::Meters(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Private>())
{
    d->ticker.setInterval(kTickMs);
    connect(&d->ticker, &QTimer::timeout, this, [this] {
        // **A frame that brought nothing still ages the holds**, so a peak
        // line goes on falling when the engine has gone quiet.
        const bool fresh = d->hub.tick();
        d->live = d->hub.isRunning();
        ++d->frame;
        if (fresh || !d->live) {
            Q_EMIT ticked();
        }
    });
    // **An address that has changed is read afresh** (ADR-0018): a window
    // on the daemon has no levels until their memory is mapped, and must
    // let go of a mapping before it goes.
    connect(TelemetryNotice::instance(), &TelemetryNotice::moved, this, [this](const QString &sessionId) {
        if (!sessionId.isEmpty() && sessionId == d->sessionId) {
            watch(sessionId);
        }
    });
}

Meters::~Meters() = default;

qulonglong Meters::frame() const
{
    return d->frame;
}

bool Meters::isLive() const
{
    return d->live;
}

void Meters::watch(const QString &sessionId)
{
    // **A spectrum asked for belongs to the session it was asked of**, and
    // that session is told to stop copying it out: a panel closed after
    // the window moved to another session lets go into a session nobody
    // is watching any more, which would leave the copy running there.
    if (sessionId != d->sessionId && !d->analysed.isEmpty()) {
        if (core::EngineFacade *engine = AppShell::engine(); engine != nullptr && !d->sessionId.isEmpty()) {
            engine->analyse(d->sessionId, QString{}, core::kBothChannels);
        }
        d->analysed.clear();
        d->analysedChannel = core::kBothChannels;
    }
    d->sessionId = sessionId;
    const core::TelemetrySlot *slot = AppShell::telemetryOf(sessionId);
    d->hub.watch(slot);
    if (slot == nullptr) {
        d->ticker.stop();
        d->live = false;
        // **Said even when there is nothing to read.** The hub has just
        // forgotten every block's hold either way, and a listener that only
        // heard about the sessions with telemetry would be a listener that
        // missed the one being opened right now.
        Q_EMIT watching();
        Q_EMIT ticked();
        return;
    }
    d->ticker.start();
    Q_EMIT watching();
}

QVariantMap Meters::levels(const QString &blockId) const
{
    const BlockLevels *block = d->hub.block(blockId);
    if (block == nullptr) {
        return {};
    }
    return QVariantMap{
        {"channelCount"_L1, static_cast<int>(block->channelCount)},
        {"rmsDb"_L1, QVariant::fromValue(asList(block->rmsDb, block->channelCount))},
        {"peakDb"_L1, QVariant::fromValue(asList(block->peakDb, block->channelCount))},
        {"holdDb"_L1, QVariant::fromValue(asList(block->holdDb, block->channelCount))},
        {"values"_L1, QVariant::fromValue(asValues(block->values))},
        {"clipped"_L1, block->clipped},
        {"clippedChannels"_L1, QVariant::fromValue(asFlags(block->clippedChannels, block->channelCount))},
        {"active"_L1, block->active},
        {"enabled"_L1, block->enabled},
        {"latencyMs"_L1, block->latencyMs},
    };
}

void Meters::analyse(const QString &blockId, int channel)
{
    const int side = channel == 0 || channel == 1 ? channel : core::kBothChannels;
    if (blockId == d->analysed && side == d->analysedChannel) {
        return;
    }
    d->analysed = blockId;
    d->analysedChannel = side;
    if (core::EngineFacade *engine = AppShell::engine(); engine != nullptr && !d->sessionId.isEmpty()) {
        engine->analyse(d->sessionId, blockId, side);
    }
}

QString Meters::analysed() const
{
    return d->analysed;
}

int Meters::analysedChannel() const
{
    return d->analysedChannel;
}

namespace
{

[[nodiscard]] QVariantList asNumbers(std::span<const float> numbers)
{
    QVariantList list;
    list.reserve(static_cast<qsizetype>(numbers.size()));
    for (const float number : numbers) {
        list.append(static_cast<qreal>(number));
    }
    return list;
}

} // namespace

QVariantList Meters::spectrum(const QString &blockId) const
{
    return asNumbers(d->hub.spectrum(blockId));
}

QVariantList Meters::inputSpectrum(const QString &blockId) const
{
    return asNumbers(d->hub.inputSpectrum(blockId));
}

QVariantList Meters::bandValues(const QString &blockId) const
{
    return asNumbers(d->hub.bandValues(blockId));
}

QVariantList Meters::spectrumBandsHz() const
{
    QVariantList centres;
    for (std::size_t band = 0; band < core::kSpectrumBands; ++band) {
        centres.append(core::spectrumBandCentreHz(band));
    }
    return centres;
}

void Meters::resetClip(const QString &blockId)
{
    d->hub.clearClip(blockId);
    Q_EMIT ticked();
}

void Meters::setHold(const QString &blockId, int milliseconds)
{
    d->hub.setHold(blockId, milliseconds);
}

void Meters::setDecay(const QString &blockId, qreal dbPerSecond)
{
    d->hub.setDecay(blockId, static_cast<float>(dbPerSecond));
}

int Meters::holdMs(const QString &blockId) const
{
    return static_cast<int>(d->hub.holdMs(blockId));
}

qreal Meters::decayDbPerSecond(const QString &blockId) const
{
    return static_cast<qreal>(d->hub.decayDbPerSecond(blockId));
}

MeterHub *Meters::hub() const
{
    return &d->hub;
}

} // namespace rb::ui

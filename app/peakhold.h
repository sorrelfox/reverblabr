// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

namespace rb::ui
{

/// What the Peak/RMS block's hold setting means in time.
///
/// **A plain table, and the QML singleton below is the same table.** What
/// speaks for a block is C++ that has no QML engine to ask a singleton of
/// — BlockModel, which follows the session — and a second copy of the
/// numbers is the sort of thing that drifts.
namespace peakHold
{

/// The settings, from the shortest wait to the longest.
[[nodiscard]] QStringList choices();

/// What the block holds for before anybody chooses: two seconds, which is
/// long enough to read a peak and short enough to see the next one.
[[nodiscard]] QString defaultChoice();

/// How long the peak line stays up, in milliseconds. Zero is "off": the
/// line follows the signal down with no wait at all.
[[nodiscard]] int holdMs(const QString &choiceId);

/// How fast it falls afterwards. Zero — never — is what "forever" means,
/// and is the one thing the hold in milliseconds cannot say.
[[nodiscard]] qreal decayDbPerSecond(const QString &choiceId);

} // namespace peakHold

/// The same, for QML.
///
/// **The block keeps the choice; the interface does the waiting.** Wall-clock
/// time lives here, in MeterHub, where a hold looks the same on a 60 Hz
/// screen and a 144 Hz one; the block has no clock of its own and no reason
/// to grow one.
///
/// **"Forever" is a line that never falls, not a very long wait.** It is the
/// decay that is switched off rather than the hold that is made enormous, so
/// that nothing has to pick a number large enough to be indistinguishable
/// from never.
///
/// The ids are the block's, spelled out again here rather than included from
/// it: the interface layer does not depend on the DSP one. A QML test checks
/// them against the catalogue the engine answers with.
class PeakHold : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The settings, from the shortest wait to the longest.
    Q_PROPERTY(QStringList choices READ choices CONSTANT)
    /// What the block holds for before anybody chooses: two seconds, which
    /// is long enough to read a peak and short enough to see the next one.
    Q_PROPERTY(QString defaultChoice READ defaultChoice CONSTANT)

public:
    explicit PeakHold(QObject *parent = nullptr);

    [[nodiscard]] QStringList choices() const;
    [[nodiscard]] QString defaultChoice() const;

    /// How long the peak line stays up, in milliseconds. Zero is "off": the
    /// line follows the signal down with no wait at all.
    [[nodiscard]] Q_INVOKABLE int holdMs(const QString &choiceId) const;

    /// How fast it falls afterwards. Zero — never — is what "forever"
    /// means, and is the one thing the hold in milliseconds cannot say.
    [[nodiscard]] Q_INVOKABLE qreal decayDbPerSecond(const QString &choiceId) const;
};

} // namespace rb::ui

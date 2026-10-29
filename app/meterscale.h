// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QList>
#include <QObject>
#include <QQmlEngine>

#include <span>

namespace rb::ui
{

/// Where a level sits on a meter.
///
/// **One mapping, shared by every meter and every scale beside one.** A bar
/// drawn with one curve and a scale drawn with another is a meter that lies
/// about itself, and it is the kind of lie nobody notices until they are
/// trying to match two sources by eye.
///
/// **Not linear in decibels.** The top of a meter is where the work happens
/// — the difference between −3 and −1 dBFS matters and the difference
/// between −50 and −48 does not — so the loud end is given room and the
/// quiet end is compressed. The curve is piecewise linear in decibels
/// through three anchors, which is enough to feel right and simple enough
/// to be checked at every tick.
namespace meterScale
{

/// The top of the meter: full scale.
inline constexpr float kTopDb = 0.0f;
/// The bottom. Anything quieter is off the scale, not half-drawn.
inline constexpr float kFloorDb = -60.0f;

/// **The top 12 dB take this much of the meter.** Roughly 45 %, which is
/// what the reference meter's spacing gives.
inline constexpr float kLoudShare = 0.45f;

/// Where \a db sits, from 0 at the floor to 1 at full scale. Clamped: a
/// level above full scale is at the top and one below the floor is at the
/// bottom, because a bar drawn past the end of its meter is not information.
[[nodiscard]] float positionOf(float db) noexcept;

/// The level at \a position, which is positionOf() the other way about.
[[nodiscard]] float dbAt(float position) noexcept;

/// Which numbers a scale is labelled with.
enum class Ticks {
    /// Every mark, for a panel with room for them.
    Full,
    /// Four marks, for a tile.
    Compact,
};

[[nodiscard]] std::span<const float> ticks(Ticks which) noexcept;

} // namespace meterScale

/// The same, for QML: a scale drawn in QML and a bar drawn in C++ have to
/// agree, so they read the one mapping rather than each having its own.
class MeterScale : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(qreal topDb READ topDb CONSTANT)
    Q_PROPERTY(qreal floorDb READ floorDb CONSTANT)
    /// Every mark, for a panel with room for them.
    Q_PROPERTY(QList<qreal> fullTicks READ fullTicks CONSTANT)
    /// Four marks, for a tile.
    Q_PROPERTY(QList<qreal> compactTicks READ compactTicks CONSTANT)

public:
    explicit MeterScale(QObject *parent = nullptr);

    [[nodiscard]] qreal topDb() const;
    [[nodiscard]] qreal floorDb() const;
    [[nodiscard]] QList<qreal> fullTicks() const;
    [[nodiscard]] QList<qreal> compactTicks() const;

    [[nodiscard]] Q_INVOKABLE qreal positionOf(qreal db) const;
    [[nodiscard]] Q_INVOKABLE qreal dbAt(qreal position) const;

    /// What an amplitude reads as. **The snapshot is amplitudes and a
    /// readout is decibels**, and the one place that turns one into the
    /// other is the one place a floor of silence is decided on.
    [[nodiscard]] Q_INVOKABLE qreal dbOf(qreal amplitude) const;
};

} // namespace rb::ui

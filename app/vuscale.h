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
#include <QStringList>

namespace rb::ui
{

/// Where a VU meter's needle points, and what its scale says.
///
/// **Linear in amplitude, not in decibels**, as a VU meter's scale always
/// was: the needle swings in proportion to the signal, so +3 VU is the end
/// of the scale, 0 VU is 71 % of the way along it and −20 VU is crowded
/// into the first 7 %. The red zone above 0 VU is therefore the last 29 %
/// of the arc, which is where the design draws it.
///
/// **The block publishes an amplitude and the reference turns it into VU.**
/// The ids of the reference settings are the block's, spelled out again
/// here rather than included from it — the interface layer does not depend
/// on the DSP one — and a QML test checks them against the catalogue.
namespace vuScale
{

/// The end of the scale, in VU.
inline constexpr float kTopVu = 3.0f;
/// The start of it. A needle below this is resting on the pin.
inline constexpr float kBottomVu = -20.0f;

/// How far along the arc \a vu is, from 0 at the pin to 1 at +3 VU.
/// Clamped: a needle does not leave its dial.
[[nodiscard]] float positionOf(float vu) noexcept;

/// What \a amplitude reads against a reference of \a referenceDb dBFS.
/// Silence reads the bottom of the scale rather than minus infinity.
[[nodiscard]] float vuOf(float amplitude, float referenceDb) noexcept;

/// The reference settings, from the quietest 0 VU to the loudest.
[[nodiscard]] QStringList referenceChoices();

/// −18 dBFS, the level a digital studio most often lines 0 VU up with.
[[nodiscard]] QString defaultReference();

/// The level in dBFS that \a choiceId puts at 0 VU; the default's for an
/// id nobody knows.
[[nodiscard]] float referenceDbOf(const QString &choiceId);

/// The marks along the arc, in VU.
[[nodiscard]] QList<qreal> marks();

} // namespace vuScale

/// The same, for QML: the dial is drawn in QML and has to agree with the
/// numbers a test reads.
class VuScale : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(qreal topVu READ topVu CONSTANT)
    Q_PROPERTY(qreal bottomVu READ bottomVu CONSTANT)
    /// Every mark the panel's dial is written with.
    Q_PROPERTY(QList<qreal> marks READ marks CONSTANT)
    Q_PROPERTY(QStringList referenceChoices READ referenceChoices CONSTANT)
    Q_PROPERTY(QString defaultReference READ defaultReference CONSTANT)

public:
    explicit VuScale(QObject *parent = nullptr);

    [[nodiscard]] qreal topVu() const;
    [[nodiscard]] qreal bottomVu() const;
    [[nodiscard]] QList<qreal> marks() const;
    [[nodiscard]] QStringList referenceChoices() const;
    [[nodiscard]] QString defaultReference() const;

    [[nodiscard]] Q_INVOKABLE qreal positionOf(qreal vu) const;
    [[nodiscard]] Q_INVOKABLE qreal vuOf(qreal amplitude, const QString &referenceId) const;
    [[nodiscard]] Q_INVOKABLE qreal referenceDbOf(const QString &referenceId) const;
};

} // namespace rb::ui

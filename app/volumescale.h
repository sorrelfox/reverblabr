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

/// The arithmetic behind the Volume block's face and panel.
///
/// **A percentage is what a person turns; an amplitude is what is kept.**
/// 100 % is the signal as it arrived, 200 % is twice it, and the number in
/// the session is the number the audio is multiplied by — so the panel shows
/// one and writes the other rather than keeping two.
///
/// **Overdrive is the top of the slider, not a multiplier.** Each setting
/// says how far the slider goes; turning it down brings the stored gain with
/// it, because a gain the slider cannot show is a gain nobody can undo.
///
/// The ids are the block's, spelled out again here rather than included from
/// it: the interface layer does not depend on the DSP one. A QML test checks
/// them against the catalogue the engine answers with, so a change on either
/// side is caught rather than guessed at.
class VolumeScale : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The overdrive settings, in the order the slider's top grows.
    Q_PROPERTY(QStringList overdriveChoices READ overdriveChoices CONSTANT)
    /// What a Volume block is before anybody touches it: unity and no
    /// overdrive, which is the descriptor's default said once for the face,
    /// the panel and the test.
    Q_PROPERTY(qreal defaultGain READ defaultGain CONSTANT)
    Q_PROPERTY(QString defaultOverdrive READ defaultOverdrive CONSTANT)

public:
    explicit VolumeScale(QObject *parent = nullptr);

    [[nodiscard]] QStringList overdriveChoices() const;
    [[nodiscard]] qreal defaultGain() const;
    [[nodiscard]] QString defaultOverdrive() const;

    /// What \a amplitude reads as on the slider: 2.0 is 200 %.
    [[nodiscard]] Q_INVOKABLE qreal percentOf(qreal amplitude) const;
    /// And back again: 200 % is 2.0, which is what is written.
    [[nodiscard]] Q_INVOKABLE qreal amplitudeOf(qreal percent) const;

    /// The largest gain \a overdriveId allows — the top of the slider.
    /// Unity for anything this build does not recognise, which is what a
    /// session written by a later version comes to.
    [[nodiscard]] Q_INVOKABLE qreal topAmplitude(const QString &overdriveId) const;

    /// \a amplitude brought within what \a overdriveId allows. **What
    /// lowering the overdrive writes**: the engine clamps what it is sent
    /// anyway, so the panel has only to send the smaller number.
    [[nodiscard]] Q_INVOKABLE qreal clampedGain(qreal amplitude, const QString &overdriveId) const;
};

} // namespace rb::ui

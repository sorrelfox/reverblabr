// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QString>
#include <QVariantMap>

#include <memory>

namespace rb::core
{

/// The Parametric EQ's response, for the interface to draw.
///
/// **The curve editor may not have a curve of its own**, for the reason
/// DynamicsCurve gives: the response is the block's cascade of bands, the
/// block follows it sample by sample, and the interface cannot reach the
/// DSP layer to ask. So the block is asked here, with the session's
/// parameters, and the answer handed up.
class EqualizerCurve
{
public:
    /// The response is worked out at this rate. The curve is drawn for a
    /// person to read, and up to 20 kHz the rates a session runs at differ
    /// by less than a line is wide.
    static constexpr double kDrawingRate = 48000.0;

    EqualizerCurve();
    ~EqualizerCurve();

    EqualizerCurve(const EqualizerCurve &) = delete;
    EqualizerCurve &operator=(const EqualizerCurve &) = delete;

    /// The type id of the block this curve belongs to.
    [[nodiscard]] static QString typeId();
    /// How many band slots the block has.
    [[nodiscard]] static int bandCount();
    /// The id of one of a band's parameters: \a which is "Used", "On",
    /// "Type", "Frequency", "Gain" or "Q"; \a band counts from nought.
    [[nodiscard]] static QString paramId(int band, const QString &which);

    /// Reads the bands out of \a params, by parameter id, as the session
    /// holds them; one the map does not mention is at its default. Answers
    /// whether the curve changed, so a drawing is rebuilt only when there
    /// is something new.
    bool setParams(const QVariantMap &params);

    /// The whole response at \a hz, in dB.
    [[nodiscard]] double magnitudeDb(double hz) const;
    /// One band's part of it, in dB: nothing for a band that is not in use
    /// or switched off.
    [[nodiscard]] double bandDb(int band, double hz) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::core

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

/// The Dynamics block's transfer curve, for the interface to draw.
///
/// **The curve editor may not have a curve of its own.** What a steady level
/// comes out at is one pure function in the DSP layer, and the block follows
/// it sample by sample; a second copy of it in the interface would be a
/// picture that slowly stopped being true of the sound. The interface layer
/// cannot reach the DSP layer (docs/architecture.md), so the one definition
/// is handed up through here instead of being written down twice.
///
/// **It is given the session's parameters, not settings.** The panel already
/// has the block's parameters by id, exactly as the session holds them; what
/// each of them means to the curve — that an infinite ratio is a click past
/// the end of the ratio slider rather than a number on it, that an absent
/// parameter is at its default — is the block's own arithmetic, and this
/// asks the block rather than repeating it.
class DynamicsCurve
{
public:
    DynamicsCurve();
    ~DynamicsCurve();

    DynamicsCurve(const DynamicsCurve &) = delete;
    DynamicsCurve &operator=(const DynamicsCurve &) = delete;

    /// The type id of the block this curve belongs to.
    [[nodiscard]] static QString typeId();

    /// Which of the block's published values is which, for a panel reading
    /// telemetry. **Asked for rather than written down**: a value that moved
    /// to another position would otherwise put the marker somewhere else.
    [[nodiscard]] static int inputLevelIndex();
    [[nodiscard]] static int outputLevelIndex();
    [[nodiscard]] static int gainReductionIndex();

    /// Reads the settings out of \a params, by parameter id, as the session
    /// holds them. A parameter the map does not mention goes back to its
    /// default, so reading the same map twice gives the same curve.
    ///
    /// Answers whether the curve changed shape, so that a drawing is rebuilt
    /// when there is something new and not when a parameter that does not
    /// bend it — an attack, a detector — was touched.
    bool setParams(const QVariantMap &params);

    /// What a steady \a inputDb comes out at, in decibels, makeup gain and
    /// all.
    [[nodiscard]] double outputDb(double inputDb) const;

    /// How much the curve takes off at that input: at or below zero, before
    /// the makeup gain.
    [[nodiscard]] double gainReductionDb(double inputDb) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::core

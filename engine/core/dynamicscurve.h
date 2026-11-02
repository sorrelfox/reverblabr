// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

#include <cstdint>
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
/// each of them means to the curve — that an expander threshold stops at the
/// threshold, that an absent parameter is at its default — is the block's own
/// arithmetic, and this asks the block rather than repeating it.
///
/// **The four points are asked for too.** Where each point of the curve
/// stands, what a point dragged somewhere means for its parameter and how far
/// it may go are all part of the curve, so a panel asks here instead of
/// working them out a second time.
class DynamicsCurve
{
public:
    /// Which curve this is. The Multiband Compressor's panel draws a band
    /// with the same plot, and a band is a compressor with a ratio and a knee.
    enum class Law : std::uint8_t {
        Dynamics, ///< The Dynamics Processor's own.
        Band, ///< A compressor: threshold, ratio, knee and makeup.
    };

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

    /// Which curve \a params are read as, from the next setParams() on.
    void setLaw(Law law);
    [[nodiscard]] Law law() const;

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

    /// What is drawn for \a inputDb. **On the Dynamics Processor's graph the
    /// make-up gain is not in it**: the gain is separate from the curve and
    /// moves the sound, and drawing it would lift the line off the diagonal
    /// the whole graph is read against. A band's curve is drawn with it.
    [[nodiscard]] double drawnDb(double inputDb) const;

    /// How much the curve takes off at that input: at or below zero, before
    /// the makeup gain.
    [[nodiscard]] double gainReductionDb(double inputDb) const;

    // The four points of the Dynamics Processor's curve, bottom to top. The
    // first runs along the floor, the next two along the diagonal, and the
    // last up the right edge.

    /// How far along the floor the first point stands: where the line of
    /// the expander comes down.
    [[nodiscard]] double cutoffDb() const;
    /// The second point: the expander starts here, at most at the threshold.
    [[nodiscard]] double expanderStartDb() const;
    /// The third point.
    [[nodiscard]] double thresholdDb() const;
    /// How high the fourth point stands at the top of the input.
    [[nodiscard]] double endDb() const;

    /// The expander ratio that puts the first point at \a cutoffDb.
    [[nodiscard]] double ratioForCutoff(double cutoffDb) const;
    /// The height that puts the fourth point at \a endDb.
    [[nodiscard]] double heightForEnd(double endDb) const;

    /// The inputs a curve has something to say about beyond its regular
    /// samples: the corners, which a polyline has to go through exactly.
    [[nodiscard]] QList<double> cornersDb() const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace rb::core

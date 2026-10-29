// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/editcommand.h"
#include "core/session.h"
#include "dsp/audiogram.h"
#include "dsp/fittingrule.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rb::core
{

/// The audiogram a listening block holds among its parameters.
///
/// A parameter the block has never been given is its default, as the
/// descriptors in dsp::audiogramParams() say: nothing measured, in dB HL. A
/// choice the build does not know counts as that default too, so an
/// audiogram written by a later version still reads.
[[nodiscard]] dsp::Audiogram audiogramFromParams(const QJsonObject &params);

/// Every parameter of \a audiogram, as a block holds them.
[[nodiscard]] QJsonObject audiogramToParams(const dsp::Audiogram &audiogram);

/// Which of a band's results a parameter holds.
enum class BandSetting : std::uint8_t {
    SoftGainDb,
    AverageGainDb,
    LoudGainDb,
    CeilingDb,
};

/// The id of the parameter that holds \a setting of band \a band, counted
/// from zero: for example "band0SoftGainDb", "band15CeilingDb". **What the
/// listening blocks name their band settings**, and so what
/// fitting writes.
[[nodiscard]] QString bandParamId(std::size_t band, BandSetting setting);

/// The ids of a control point's parameters, counted from zero, in a block
/// set through points (Hearing Compensation 64): whether it is on,
/// where it is, and its settings ("point0On", "point0Hz",
/// "point0SoftGainDb", … "point23CeilingDb").
[[nodiscard]] QString pointOnParamId(std::size_t point);
[[nodiscard]] QString pointHzParamId(std::size_t point);
[[nodiscard]] QString pointParamId(std::size_t point, BandSetting setting);

/// How many control points a block set through them has.
inline constexpr std::size_t kHearingPoints = 24;

/// What "Fit from audiogram" does to a block, and what it found.
struct AudiogramFit {
    /// Each band's result, low to high, including what the panel reports:
    /// no response, or more than the most gain a band may have. In a block
    /// set through points, each point's.
    std::vector<dsp::BandFit> bands;
    /// Where each result is, in hertz: a band's centre, or a point's place.
    std::vector<double> centresHz;
    /// **One edit**: every band setting, as SetParam commands to be applied
    /// as a list, so that it is applied whole and taken back in one undo
    /// (editCommandsFromJson()).
    std::vector<EditCommand> commands;
};

/// Fits \a block's own audiogram to bands centred at \a centresHz (the
/// block's layout) with \a options, and writes the result as the block's
/// band settings.
///
/// Only the settings that would change are in the edit, so fitting twice in
/// a row leaves nothing to apply the second time.
[[nodiscard]] AudiogramFit fitFromAudiogram(const BlockInstance &block,
                                            std::span<const double> centresHz,
                                            const dsp::FittingOptions &options,
                                            float ceilingOffsetDb = 0.0f);

/// **Fits \a block's own audiogram to control points**: a point at
/// each frequency the audiogram has a level for, heard or not, fitted
/// there, and every other point off. The curves through the points then set
/// the bands, as a person placing the same points would. Nothing measured,
/// nothing to fit: no edit.
[[nodiscard]] AudiogramFit
fitPointsFromAudiogram(const BlockInstance &block, const dsp::FittingOptions &options, float ceilingOffsetDb = 0.0f);

/// How many bands a listening block of type \a typeId has, or 0 for a type
/// with none: 16 for Hearing Compensation 16, 64 for 64.
[[nodiscard]] std::size_t hearingBandCount(const QString &typeId);

/// Whether a listening block of type \a typeId is set through control
/// points rather than band by band.
[[nodiscard]] bool hearingBlockHasPoints(const QString &typeId);

/// **Fit from audiogram for a Hearing Compensation block**: the
/// block's own band layout (ADR-0019's ERB-spaced centres), or for a block
/// set through points a point at each measured frequency; the
/// fitting options for the unit its audiogram is in; and the ceilings
/// written on the block's listening scale (dsp::kListeningOffsetDb above
/// dBFS) whichever unit that is. Nothing for a block with no bands.
[[nodiscard]] std::optional<AudiogramFit> fitHearingBlock(const BlockInstance &block);

[[nodiscard]] QJsonArray editListJson(std::span<const EditCommand> commands);

} // namespace rb::core

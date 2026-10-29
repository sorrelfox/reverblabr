// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "volumescale.h"

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// Each overdrive setting and the top of the slider it makes. The block
/// works the same number out from the choice's position; here it is written
/// down, so that reordering the choices one day changes one table and not
/// the meaning of a saved session.
struct Overdrive {
    QLatin1StringView id;
    qreal top;
};

constexpr std::array<Overdrive, 4> kOverdrives{{
    {"off"_L1, 1.0},
    {"2x"_L1, 2.0},
    {"3x"_L1, 3.0},
    {"4x"_L1, 4.0},
}};

} // namespace

VolumeScale::VolumeScale(QObject *parent)
    : QObject(parent)
{
}

QStringList VolumeScale::overdriveChoices() const
{
    QStringList choices;
    choices.reserve(static_cast<qsizetype>(kOverdrives.size()));
    for (const Overdrive &overdrive : kOverdrives) {
        choices.append(QString(overdrive.id));
    }
    return choices;
}

qreal VolumeScale::defaultGain() const
{
    return 1.0;
}

QString VolumeScale::defaultOverdrive() const
{
    return QString(kOverdrives.front().id);
}

qreal VolumeScale::percentOf(qreal amplitude) const
{
    return amplitude * 100.0;
}

qreal VolumeScale::amplitudeOf(qreal percent) const
{
    return percent / 100.0;
}

qreal VolumeScale::topAmplitude(const QString &overdriveId) const
{
    const auto found = std::ranges::find_if(
        kOverdrives, [&overdriveId](const Overdrive &overdrive) { return overdriveId == overdrive.id; });
    // **Unity for a setting this build has never heard of.** A session
    // written by a later version is read by an older one, and a slider that
    // went to some number nobody could name would be worse than one that
    // went to 100 %.
    return found == kOverdrives.end() ? 1.0 : found->top;
}

qreal VolumeScale::clampedGain(qreal amplitude, const QString &overdriveId) const
{
    return std::clamp(amplitude, 0.0, topAmplitude(overdriveId));
}

} // namespace rb::ui

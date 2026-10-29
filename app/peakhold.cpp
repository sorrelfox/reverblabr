// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "peakhold.h"

#include "meterhub.h"

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace
{

/// Each setting, how long it waits, and whether it ever comes down.
struct Choice {
    QLatin1StringView id;
    int holdMs;
    bool falls;
};

constexpr std::array<Choice, 5> kChoices{{
    {"off"_L1, 0, true},
    {"1s"_L1, 1000, true},
    {"2s"_L1, 2000, true},
    {"5s"_L1, 5000, true},
    // The wait is beside the point when nothing ever falls; it is written
    // down all the same, so that switching away from "forever" does not
    // leave a line hanging on a number from somewhere else.
    {"forever"_L1, 0, false},
}};

[[nodiscard]] const Choice &choiceOf(const QString &choiceId)
{
    const auto found =
        std::ranges::find_if(kChoices, [&choiceId](const Choice &choice) { return choiceId == choice.id; });
    // **The default for a setting this build has never heard of.** A session
    // written by a later version is read by an older one, and a meter that
    // held for some unnamed time would be a meter nobody could explain.
    return found == kChoices.end() ? kChoices[2] : *found;
}

} // namespace

namespace peakHold
{

QStringList choices()
{
    QStringList ids;
    ids.reserve(static_cast<qsizetype>(kChoices.size()));
    for (const Choice &choice : kChoices) {
        ids.append(QString(choice.id));
    }
    return ids;
}

QString defaultChoice()
{
    return QString(kChoices[2].id);
}

int holdMs(const QString &choiceId)
{
    return choiceOf(choiceId).holdMs;
}

qreal decayDbPerSecond(const QString &choiceId)
{
    return choiceOf(choiceId).falls ? static_cast<qreal>(MeterHub::kDecayDbPerSecond) : 0.0;
}

} // namespace peakHold

PeakHold::PeakHold(QObject *parent)
    : QObject(parent)
{
}

QStringList PeakHold::choices() const
{
    return peakHold::choices();
}

QString PeakHold::defaultChoice() const
{
    return peakHold::defaultChoice();
}

int PeakHold::holdMs(const QString &choiceId) const
{
    return peakHold::holdMs(choiceId);
}

qreal PeakHold::decayDbPerSecond(const QString &choiceId) const
{
    return peakHold::decayDbPerSecond(choiceId);
}

} // namespace rb::ui

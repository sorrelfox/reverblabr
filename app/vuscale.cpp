// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "vuscale.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace rb::ui
{

namespace vuScale
{

namespace
{

struct Reference {
    QLatin1StringView id;
    float db;
};

// The block's ids and what they mean, quietest first, as the block lists
// them.
constexpr std::array kReferences{
    Reference{"-20"_L1, -20.0f},
    Reference{"-18"_L1, -18.0f},
    Reference{"-14"_L1, -14.0f},
};

constexpr std::size_t kDefaultReference = 1;

[[nodiscard]] float amplitudeOf(float vu) noexcept
{
    return std::pow(10.0f, vu / 20.0f);
}

} // namespace

float positionOf(float vu) noexcept
{
    if (!(vu > kBottomVu)) { // NaN is on the pin too
        return 0.0f;
    }
    return std::clamp(amplitudeOf(vu) / amplitudeOf(kTopVu), 0.0f, 1.0f);
}

float vuOf(float amplitude, float referenceDb) noexcept
{
    if (!(amplitude > 0.0f)) {
        return kBottomVu;
    }
    return std::max(kBottomVu, 20.0f * std::log10(amplitude) - referenceDb);
}

QStringList referenceChoices()
{
    QStringList ids;
    for (const Reference &reference : kReferences) {
        ids.append(reference.id);
    }
    return ids;
}

QString defaultReference()
{
    return kReferences[kDefaultReference].id;
}

float referenceDbOf(const QString &choiceId)
{
    const auto it = std::ranges::find_if(kReferences, [&choiceId](const Reference &r) { return r.id == choiceId; });
    return it == kReferences.end() ? kReferences[kDefaultReference].db : it->db;
}

QList<qreal> marks()
{
    return {-20.0, -10.0, -7.0, -5.0, -3.0, -2.0, -1.0, 0.0, 1.0, 2.0, 3.0};
}

} // namespace vuScale

VuScale::VuScale(QObject *parent)
    : QObject(parent)
{
}

qreal VuScale::topVu() const
{
    return vuScale::kTopVu;
}

qreal VuScale::bottomVu() const
{
    return vuScale::kBottomVu;
}

QList<qreal> VuScale::marks() const
{
    return vuScale::marks();
}

QStringList VuScale::referenceChoices() const
{
    return vuScale::referenceChoices();
}

QString VuScale::defaultReference() const
{
    return vuScale::defaultReference();
}

qreal VuScale::positionOf(qreal vu) const
{
    return vuScale::positionOf(static_cast<float>(vu));
}

qreal VuScale::vuOf(qreal amplitude, const QString &referenceId) const
{
    return vuScale::vuOf(static_cast<float>(amplitude), vuScale::referenceDbOf(referenceId));
}

qreal VuScale::referenceDbOf(const QString &referenceId) const
{
    return vuScale::referenceDbOf(referenceId);
}

} // namespace rb::ui

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/equalizercurve.h"

#include "blocks/parametriceq.h"
#include "core/paramjson.h"

#include <QJsonValue>

#include <array>
#include <expected>
#include <optional>
#include <span>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::core
{

class EqualizerCurve::Private
{
public:
    /// **A block, not a copy of its arithmetic**: which bands count, what
    /// each type is, what an absent parameter means — the block already
    /// knows, and asking it keeps the drawing and the sound one thing.
    blocks::ParametricEq block;
    std::array<blocks::ParametricEq::Band, blocks::ParametricEq::kBandCount> bands = block.bands();
};

EqualizerCurve::EqualizerCurve()
    : d(std::make_unique<Private>())
{
}

EqualizerCurve::~EqualizerCurve() = default;

QString EqualizerCurve::typeId()
{
    return QString::fromUtf8(blocks::ParametricEq::kTypeId.data(),
                             static_cast<qsizetype>(blocks::ParametricEq::kTypeId.size()));
}

int EqualizerCurve::bandCount()
{
    return static_cast<int>(blocks::ParametricEq::kBandCount);
}

QString EqualizerCurve::paramId(int band, const QString &which)
{
    return u"band"_s + QString::number(band + 1) + which;
}

bool EqualizerCurve::setParams(const QVariantMap &params)
{
    // Everything back to its defaults first, as DynamicsCurve does: a panel
    // that opened a second block would otherwise draw the first one's bands
    // wherever the new session says nothing.
    const std::expected<void, dsp::BlockError> cleared = d->block.loadState({});
    static_cast<void>(cleared);

    for (const dsp::ParamDescriptor &descriptor : d->block.params()) {
        const QVariantMap::const_iterator found = params.constFind(QString::fromStdString(descriptor.id));
        if (found == params.constEnd()) {
            continue;
        }
        std::optional<dsp::ParamValue> value = valueFromJson(descriptor, QJsonValue::fromVariant(*found));
        if (!value.has_value()) {
            continue;
        }
        // One bad value costs one parameter, not the curve.
        const std::expected<void, dsp::BlockError> applied = d->block.setParam(descriptor.id, *std::move(value));
        static_cast<void>(applied);
    }

    const auto bands = d->block.bands();
    if (bands == d->bands) {
        return false;
    }
    d->bands = bands;
    return true;
}

double EqualizerCurve::magnitudeDb(double hz) const
{
    return blocks::ParametricEq::curveDb(hz, d->bands, kDrawingRate);
}

double EqualizerCurve::bandDb(int band, double hz) const
{
    if (band < 0 || band >= bandCount()) {
        return 0.0;
    }
    return blocks::ParametricEq::curveDb(
        hz, std::span(d->bands).subspan(static_cast<std::size_t>(band), 1), kDrawingRate);
}

} // namespace rb::core

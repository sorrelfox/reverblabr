// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/dynamicscurve.h"

#include "blocks/dynamics/dynamics.h"
#include "core/paramjson.h"
#include "dsp/dynamics.h"

#include <QJsonValue>

#include <expected>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::core
{

class DynamicsCurve::Private
{
public:
    /// **A block, not a copy of its settings.** Working the curve's settings
    /// out of the parameters is arithmetic the block already does for the
    /// audio thread, and doing it a second time here is how a panel starts
    /// drawing something the sound does not do.
    blocks::Dynamics block;
    dsp::DynamicsShape shape = block.settings();

    Law law = Law::Dynamics;
    dsp::DynamicsSettings band;
};

DynamicsCurve::DynamicsCurve()
    : d(std::make_unique<Private>())
{
}

DynamicsCurve::~DynamicsCurve() = default;

QString DynamicsCurve::typeId()
{
    return QString::fromUtf8(blocks::Dynamics::kTypeId.data(),
                             static_cast<qsizetype>(blocks::Dynamics::kTypeId.size()));
}

int DynamicsCurve::inputLevelIndex()
{
    return static_cast<int>(blocks::Dynamics::kInputLevelValue);
}

int DynamicsCurve::outputLevelIndex()
{
    return static_cast<int>(blocks::Dynamics::kOutputLevelValue);
}

int DynamicsCurve::gainReductionIndex()
{
    return static_cast<int>(blocks::Dynamics::kGainReductionValue);
}

void DynamicsCurve::setLaw(Law law)
{
    d->law = law;
}

DynamicsCurve::Law DynamicsCurve::law() const
{
    return d->law;
}

bool DynamicsCurve::setParams(const QVariantMap &params)
{
    if (d->law == Law::Band) {
        const auto number = [&params](const QString &id, float fallback) {
            return static_cast<float>(params.value(id, static_cast<double>(fallback)).toDouble());
        };
        dsp::DynamicsSettings band;
        band.thresholdDb = number(u"threshold"_s, band.thresholdDb);
        band.ratio = number(u"ratio"_s, band.ratio);
        band.kneeDb = number(u"knee"_s, band.kneeDb);
        band.makeupDb = number(u"makeup"_s, band.makeupDb);
        if (band == d->band) {
            return false;
        }
        d->band = band;
        return true;
    }

    // Everything back to its defaults first: a panel that opened a second
    // block would otherwise be drawing the first one's settings wherever the
    // new session says nothing.
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
        // **One bad value costs one parameter, not the curve.** A session
        // written by a later version may hold a number outside the range
        // this build allows; leaving that parameter at its default draws a
        // curve that is wrong in one place rather than one that is wrong
        // everywhere.
        const std::expected<void, dsp::BlockError> applied = d->block.setParam(descriptor.id, *std::move(value));
        static_cast<void>(applied);
    }

    const dsp::DynamicsShape shape = d->block.settings();
    if (shape == d->shape) {
        return false;
    }
    d->shape = shape;
    return true;
}

double DynamicsCurve::outputDb(double inputDb) const
{
    const auto input = static_cast<float>(inputDb);
    return static_cast<double>(d->law == Law::Band ? dsp::outputDb(input, d->band) : dsp::outputDb(input, d->shape));
}

double DynamicsCurve::drawnDb(double inputDb) const
{
    return d->law == Law::Band ? outputDb(inputDb) : inputDb + gainReductionDb(inputDb);
}

double DynamicsCurve::gainReductionDb(double inputDb) const
{
    const auto input = static_cast<float>(inputDb);
    return static_cast<double>(d->law == Law::Band ? dsp::gainReductionDb(input, d->band)
                                                   : dsp::gainReductionDb(input, d->shape));
}

double DynamicsCurve::cutoffDb() const
{
    return static_cast<double>(dsp::dynamicsCutoffDb(d->shape));
}

double DynamicsCurve::expanderStartDb() const
{
    return static_cast<double>(dsp::expanderStartDb(d->shape));
}

double DynamicsCurve::thresholdDb() const
{
    return static_cast<double>(d->shape.thresholdDb);
}

double DynamicsCurve::endDb() const
{
    return drawnDb(static_cast<double>(dsp::kDynamicsTopDb));
}

double DynamicsCurve::ratioForCutoff(double cutoffDb) const
{
    return static_cast<double>(dsp::dynamicsRatioFor(dsp::expanderStartDb(d->shape), static_cast<float>(cutoffDb)));
}

double DynamicsCurve::heightForEnd(double endDb) const
{
    return static_cast<double>(
        dsp::dynamicsHeightFor(d->shape.thresholdDb, dsp::kDynamicsTopDb, static_cast<float>(endDb)));
}

QList<double> DynamicsCurve::cornersDb() const
{
    if (d->law == Law::Band) {
        return {};
    }
    return {cutoffDb(), expanderStartDb(), thresholdDb()};
}

} // namespace rb::core

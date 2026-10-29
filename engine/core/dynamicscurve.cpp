// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/dynamicscurve.h"

#include "blocks/dynamics.h"
#include "core/paramjson.h"
#include "dsp/dynamics.h"

#include <QJsonValue>

#include <expected>
#include <optional>
#include <utility>

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
    dsp::DynamicsSettings settings = block.settings();
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

bool DynamicsCurve::setParams(const QVariantMap &params)
{
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

    const dsp::DynamicsSettings settings = d->block.settings();
    if (settings == d->settings) {
        return false;
    }
    d->settings = settings;
    return true;
}

double DynamicsCurve::outputDb(double inputDb) const
{
    return static_cast<double>(dsp::outputDb(static_cast<float>(inputDb), d->settings));
}

double DynamicsCurve::gainReductionDb(double inputDb) const
{
    return static_cast<double>(dsp::gainReductionDb(static_cast<float>(inputDb), d->settings));
}

} // namespace rb::core

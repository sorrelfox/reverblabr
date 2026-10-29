// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/compressormodes.h"

#include "blocks/dynamics.h"
#include "blocks/simplecompressor.h"
#include "dsp/dynamics.h"

#include <string_view>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

[[nodiscard]] QString fromView(std::string_view view)
{
    return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
}

} // namespace

QString CompressorModes::typeId()
{
    return fromView(blocks::SimpleCompressor::kTypeId);
}

QStringList CompressorModes::ids()
{
    QStringList ids;
    for (const blocks::SimpleCompressor::Mode &mode : blocks::SimpleCompressor::modes()) {
        ids.append(fromView(mode.id));
    }
    return ids;
}

QString CompressorModes::defaultId()
{
    return fromView(blocks::SimpleCompressor::kDefaultMode);
}

QVariantMap CompressorModes::settings(const QString &id)
{
    const blocks::SimpleCompressor::Mode *mode = blocks::SimpleCompressor::findMode(id.toStdString());
    if (mode == nullptr) {
        return {};
    }
    // The detector by the Dynamics block's own ids, so a mode reads the way
    // that block's detector setting does.
    const std::string_view detector = mode->timing.detector == dsp::DynamicsDetector::Rms
        ? blocks::Dynamics::kDetectorRms
        : blocks::Dynamics::kDetectorPeak;
    return {
        {u"threshold"_s, static_cast<double>(mode->settings.thresholdDb)},
        {u"ratio"_s, static_cast<double>(mode->settings.ratio)},
        {u"knee"_s, static_cast<double>(mode->settings.kneeDb)},
        {u"makeup"_s, static_cast<double>(mode->settings.makeupDb)},
        {u"attack"_s, static_cast<double>(mode->timing.attackMs)},
        {u"release"_s, static_cast<double>(mode->timing.releaseMs)},
        {u"detector"_s, fromView(detector)},
    };
}

} // namespace rb::core

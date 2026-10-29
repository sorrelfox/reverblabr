// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/audiogramfit.h"

#include "dsp/erbscale.h"
#include "dsp/hearingbands.h"

#include <QJsonValue>

#include <array>
#include <cmath>
#include <string>
#include <utility>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

[[nodiscard]] QString idOf(std::size_t point, dsp::AudiogramField field)
{
    return QString::fromStdString(dsp::audiogramParamId(point, field));
}

[[nodiscard]] QString nameOf(std::string_view name)
{
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

/// A band setting as it is written: to a tenth of a decibel, which is finer
/// than anybody hears and coarse enough that the same fit twice gives the
/// same numbers rather than two that differ in the seventh digit.
[[nodiscard]] double rounded(float db)
{
    return std::round(static_cast<double>(db) * 10.0) / 10.0;
}

} // namespace

dsp::Audiogram audiogramFromParams(const QJsonObject &params)
{
    dsp::Audiogram audiogram;
    const QString unit = params.value(nameOf(dsp::kAudiogramUnitParamId)).toString();
    audiogram.unit = dsp::audiogramUnitFromName(unit.toStdString()).value_or(dsp::AudiogramUnit::DbHl);

    for (std::size_t point = 0; point < dsp::kAudiogramPoints; ++point) {
        dsp::AudiogramPoint &at = audiogram.points[point];
        const QString mark = params.value(idOf(point, dsp::AudiogramField::Mark)).toString();
        at.mark = dsp::audiogramMarkFromName(mark.toStdString()).value_or(dsp::AudiogramMark::None);
        at.levelDb = static_cast<float>(params.value(idOf(point, dsp::AudiogramField::LevelDb)).toDouble(0.0));
        if (params.value(idOf(point, dsp::AudiogramField::TooLoudGiven)).toBool(false)) {
            const QJsonValue tooLoud = params.value(idOf(point, dsp::AudiogramField::TooLoudDb));
            if (tooLoud.isDouble()) {
                at.tooLoudDb = static_cast<float>(tooLoud.toDouble());
            }
        }
    }
    return audiogram;
}

QJsonObject audiogramToParams(const dsp::Audiogram &audiogram)
{
    QJsonObject params;
    params.insert(nameOf(dsp::kAudiogramUnitParamId), nameOf(dsp::audiogramUnitName(audiogram.unit)));
    for (std::size_t point = 0; point < dsp::kAudiogramPoints; ++point) {
        const dsp::AudiogramPoint &at = audiogram.points[point];
        params.insert(idOf(point, dsp::AudiogramField::Mark), nameOf(dsp::audiogramMarkName(at.mark)));
        params.insert(idOf(point, dsp::AudiogramField::LevelDb), static_cast<double>(at.levelDb));
        params.insert(idOf(point, dsp::AudiogramField::TooLoudGiven), at.tooLoudDb.has_value());
        if (at.tooLoudDb) {
            params.insert(idOf(point, dsp::AudiogramField::TooLoudDb), static_cast<double>(*at.tooLoudDb));
        }
    }
    return params;
}

QString bandParamId(std::size_t band, BandSetting setting)
{
    QString suffix;
    switch (setting) {
    case BandSetting::SoftGainDb:
        suffix = u"SoftGainDb"_s;
        break;
    case BandSetting::AverageGainDb:
        suffix = u"AverageGainDb"_s;
        break;
    case BandSetting::LoudGainDb:
        suffix = u"LoudGainDb"_s;
        break;
    case BandSetting::CeilingDb:
        suffix = u"CeilingDb"_s;
        break;
    }
    return u"band"_s + QString::number(band) + suffix;
}

QString pointOnParamId(std::size_t point)
{
    return u"point"_s + QString::number(point) + u"On"_s;
}

QString pointHzParamId(std::size_t point)
{
    return u"point"_s + QString::number(point) + u"Hz"_s;
}

QString pointParamId(std::size_t point, BandSetting setting)
{
    // The same four as a band's, after "point<N>" rather than "band<N>".
    return u"point"_s + QString::number(point) + bandParamId(0, setting).mid(5);
}

AudiogramFit fitFromAudiogram(const BlockInstance &block,
                              std::span<const double> centresHz,
                              const dsp::FittingOptions &options,
                              float ceilingOffsetDb)
{
    AudiogramFit fit;
    fit.bands = dsp::fitAudiogram(audiogramFromParams(block.params), centresHz, options);
    fit.centresHz.assign(centresHz.begin(), centresHz.end());
    for (std::size_t band = 0; band < fit.bands.size(); ++band) {
        const dsp::BandFit &result = fit.bands[band];
        const std::array<std::pair<BandSetting, float>, 4> settings{{
            {BandSetting::SoftGainDb, result.softGainDb},
            {BandSetting::AverageGainDb, result.averageGainDb},
            {BandSetting::LoudGainDb, result.loudGainDb},
            {BandSetting::CeilingDb, result.ceilingDb + ceilingOffsetDb},
        }};
        for (const auto &[setting, db] : settings) {
            const QString key = bandParamId(band, setting);
            const QJsonValue value{rounded(db)};
            if (block.params.value(key) != value) {
                fit.commands.emplace_back(SetParam{block.id, key, value});
            }
        }
    }
    return fit;
}

AudiogramFit
fitPointsFromAudiogram(const BlockInstance &block, const dsp::FittingOptions &options, float ceilingOffsetDb)
{
    const dsp::Audiogram audiogram = audiogramFromParams(block.params);
    std::vector<double> where;
    for (std::size_t point = 0; point < dsp::kAudiogramPoints && where.size() < kHearingPoints; ++point) {
        if (audiogram.points[point].mark != dsp::AudiogramMark::None) {
            where.push_back(dsp::kAudiogramFrequenciesHz[point]);
        }
    }
    AudiogramFit fit;
    if (where.empty()) {
        return fit;
    }
    fit.bands = dsp::fitAudiogram(audiogram, where, options);
    fit.centresHz = where;
    const auto write = [&](const QString &key, const QJsonValue &value) {
        if (block.params.value(key) != value) {
            fit.commands.emplace_back(SetParam{block.id, key, value});
        }
    };
    for (std::size_t point = 0; point < kHearingPoints; ++point) {
        if (point >= fit.bands.size()) {
            write(pointOnParamId(point), QJsonValue{false});
            continue;
        }
        const dsp::BandFit &result = fit.bands[point];
        write(pointOnParamId(point), QJsonValue{true});
        write(pointHzParamId(point), QJsonValue{where[point]});
        write(pointParamId(point, BandSetting::SoftGainDb), QJsonValue{rounded(result.softGainDb)});
        write(pointParamId(point, BandSetting::AverageGainDb), QJsonValue{rounded(result.averageGainDb)});
        write(pointParamId(point, BandSetting::LoudGainDb), QJsonValue{rounded(result.loudGainDb)});
        write(pointParamId(point, BandSetting::CeilingDb), QJsonValue{rounded(result.ceilingDb + ceilingOffsetDb)});
    }
    return fit;
}

std::size_t hearingBandCount(const QString &typeId)
{
    if (typeId == u"hearing.compensation16"_s) {
        return 16;
    }
    if (typeId == u"hearing.compensation64"_s) {
        return 64;
    }
    return 0;
}

bool hearingBlockHasPoints(const QString &typeId)
{
    return typeId == u"hearing.compensation64"_s;
}

std::optional<AudiogramFit> fitHearingBlock(const BlockInstance &block)
{
    const std::size_t bands = hearingBandCount(block.type);
    if (bands == 0) {
        return std::nullopt;
    }
    const dsp::Audiogram audiogram = audiogramFromParams(block.params);
    dsp::FittingOptions options = dsp::defaultFittingOptions(audiogram.unit);
    // **The ceilings on the block's own scale.** In dB HL the fitting rule
    // puts average sound at 45 dB, as the block's listening scale does. A
    // dBFS audiogram's levels are the block's band levels themselves, so
    // average sound goes where the block's average is and each ceiling is
    // moved up onto the listening scale.
    float ceilingOffsetDb = 0.0f;
    if (audiogram.unit == dsp::AudiogramUnit::DbFs) {
        options.averageLevelDb = dsp::kAverageInputDbfs;
        ceilingOffsetDb = dsp::kListeningOffsetDb;
    }
    if (hearingBlockHasPoints(block.type)) {
        return fitPointsFromAudiogram(block, options, ceilingOffsetDb);
    }
    const std::vector<double> centres = dsp::erbCentres(bands);
    return fitFromAudiogram(block, centres, options, ceilingOffsetDb);
}

QJsonArray editListJson(std::span<const EditCommand> commands)
{
    QJsonArray list;
    for (const EditCommand &command : commands) {
        list.append(editCommandToJson(command));
    }
    return list;
}

} // namespace rb::core

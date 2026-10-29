// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/livestreams.h"

#include "blocks/livestream.h"
#include "core/broadcasts.h"
#include "dsp/block.h"

#include <QThread>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

[[nodiscard]] stream::LiveQualityPreset presetFromId(std::string_view id)
{
    if (id == blocks::LiveStream::kQualityGood) {
        return stream::LiveQualityPreset::Good;
    }
    if (id == blocks::LiveStream::kQualityBest) {
        return stream::LiveQualityPreset::Best;
    }
    return stream::LiveQualityPreset::Higher;
}

[[nodiscard]] stream::Visualisation visualisationFromId(std::string_view id)
{
    if (id == blocks::LiveStream::kVisualNone) {
        return stream::Visualisation::None;
    }
    if (id == blocks::LiveStream::kVisualSpectrum) {
        return stream::Visualisation::Spectrum;
    }
    if (id == blocks::LiveStream::kVisualMeter) {
        return stream::Visualisation::Meter;
    }
    return stream::Visualisation::Waveform;
}

} // namespace

stream::LiveStreamSettings liveStreamSettings(const dsp::Block &block, const QString &sessionName)
{
    stream::LiveStreamSettings settings;
    settings.url = QString::fromStdString(block.param("url").text()).trimmed().toStdString();
    settings.quality = stream::liveQuality(presetFromId(block.param("quality").text()));
    settings.encoder = stream::videoEncoderFromId(block.param("encoder").text());
    settings.sessionName = sessionName.toStdString();
    // **The title takes the session's name** by its token, as a stream's
    // title template does; any other text is itself.
    settings.scene.title = stream::renderTitle(block.param("title").text(), stream::Track{}, settings.sessionName);
    settings.scene.subtitle =
        stream::renderTitle(block.param("subtitle").text(), stream::Track{}, settings.sessionName);
    settings.scene.background = stream::parseColour(block.param("background").text())
                                    .value_or(*stream::parseColour(blocks::LiveStream::kDefaultBackground));
    settings.scene.backgroundImage = homePath(block.param("backgroundImage").text());
    settings.scene.logo = homePath(block.param("logo").text());
    settings.scene.visualisation = visualisationFromId(block.param("visualisation").text());
    settings.titleTemplate = block.param("titleTemplate").text();
    settings.nowPlayingFile = nowPlayingFileOf(block);
    return settings;
}

QJsonObject liveStreamStatusJson(const stream::LiveStreamStatus &status)
{
    QJsonObject json = broadcastStatusJson(status.stream);
    json.insert(u"encoder"_s, QString::fromStdString(status.encoder));
    json.insert(u"width"_s, status.width);
    json.insert(u"height"_s, status.height);
    json.insert(u"fps"_s, status.fps);
    json.insert(u"framesSent"_s, static_cast<double>(status.framesSent));
    json.insert(u"framesDropped"_s, static_cast<double>(status.framesDropped));
    return json;
}

UnavailableChoices liveStreamUnavailableChoices(const stream::VideoEncoderProbe &probe)
{
    std::map<std::string, std::string> encoders;
    for (const stream::VideoEncoderKind kind : stream::allVideoEncoders()) {
        if (!probe.has(kind)) {
            encoders.emplace(std::string{stream::videoEncoderId(kind)}, "encoderMissing");
        }
    }
    if (!probe.any()) {
        encoders.emplace(std::string{blocks::LiveStream::kEncoderAuto}, "noH264Encoder");
    }
    UnavailableChoices unavailable;
    if (!encoders.empty()) {
        unavailable.emplace(std::pair{std::string{blocks::LiveStream::kTypeId}, std::string{"encoder"}}, encoders);
    }
    return unavailable;
}

UnavailableChoices liveStreamUnavailableChoices()
{
    return liveStreamUnavailableChoices(
        stream::finishedVideoEncoderProbe().value_or(stream::probeSoftwareVideoEncoders()));
}

bool probeVideoEncodersFor(QObject *receiver, std::function<void()> whenDone)
{
    if (stream::finishedVideoEncoderProbe()) {
        return false;
    }
    // A thread of Qt's, so that the answer comes back through a connection
    // that is let go with the receiver; two engines asking at once both
    // wait for the one probe the process makes.
    QThread *probing = QThread::create([] { (void)stream::probeVideoEncoders(); });
    QObject::connect(probing, &QThread::finished, receiver, std::move(whenDone));
    QObject::connect(probing, &QThread::finished, probing, &QObject::deleteLater);
    probing->start();
    return true;
}

} // namespace rb::core

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/broadcasts.h"

#include "blocks/broadcast.h"
#include "buildversion.h"
#include "dsp/block.h"

#include <QDir>

#include <algorithm>
#include <cmath>
#include <string>

using namespace Qt::StringLiterals;

namespace rb::core
{

namespace
{

[[nodiscard]] std::optional<stream::StreamFormat> formatFromId(std::string_view id)
{
    if (id == blocks::Broadcast::kFormatMp3) {
        return stream::StreamFormat::Mp3;
    }
    if (id == blocks::Broadcast::kFormatOpus) {
        return stream::StreamFormat::Opus;
    }
    if (id == blocks::Broadcast::kFormatVorbis) {
        return stream::StreamFormat::Vorbis;
    }
    if (id == blocks::Broadcast::kFormatAac) {
        return stream::StreamFormat::Aac;
    }
    return std::nullopt;
}

[[nodiscard]] std::string trimmed(const std::string &text)
{
    return QString::fromStdString(text).trimmed().toStdString();
}

} // namespace

stream::BroadcastSettings broadcastSettings(const dsp::Block &block, const QString &sessionName)
{
    stream::BroadcastSettings settings;
    settings.server.kind = block.param("server").text() == blocks::Broadcast::kServerShoutcast
        ? stream::ServerKind::Shoutcast
        : stream::ServerKind::Icecast;
    settings.server.host = trimmed(block.param("host").text());
    settings.server.port =
        static_cast<std::uint16_t>(std::clamp(block.param("port").asNumber().value_or(8000.0), 0.0, 65535.0));
    settings.server.mount = trimmed(block.param("mount").text());
    settings.server.user = trimmed(block.param("user").text());
    if (settings.server.user.empty()) {
        settings.server.user = "source";
    }
    settings.server.userAgent = "Reverblabr/" + std::string{version()};

    settings.station.name = block.param("stationName").text();
    settings.station.genre = block.param("genre").text();
    settings.station.url = block.param("url").text();
    settings.station.description = block.param("description").text();
    settings.station.listed = block.param("listed").asBool().value_or(false);

    settings.format = formatFromId(block.param("format").text()).value_or(stream::StreamFormat::Mp3);
    settings.mono = block.param("channels").text() == blocks::Broadcast::kChannelsMono;
    // **Within what the format takes for so many channels**: a setting the
    // encoder would refuse is the nearest one it takes.
    settings.bitrateKbps =
        stream::clampedBitrate(settings.format,
                               settings.mono ? 1 : 2,
                               static_cast<std::uint32_t>(block.param("bitrate").asNumber().value_or(128.0)));
    settings.titleTemplate = block.param("titleTemplate").text();
    settings.sessionName = sessionName.toStdString();

    if (block.param("nowPlaying").text() == blocks::Broadcast::kNowPlayingFile) {
        QString file = QString::fromStdString(block.param("nowPlayingFile").text()).trimmed();
        if (file == "~"_L1 || file.startsWith("~/"_L1)) {
            file = QDir::homePath() + file.mid(1);
        }
        settings.nowPlayingFile = std::filesystem::path{file.toStdString()};
    }
    return settings;
}

QString broadcastSecretKey(const QString &sessionId, const QString &blockId)
{
    return u"broadcast/%1/%2"_s.arg(sessionId, blockId);
}

QJsonObject broadcastStatusJson(const stream::BroadcastStatus &status)
{
    const auto text = [](std::string_view view) {
        return QString::fromUtf8(view.data(), static_cast<qsizetype>(view.size()));
    };
    QJsonObject json{
        {u"state"_s, text(stream::broadcastStateName(status.state))},
        {u"connectedSince"_s, static_cast<double>(status.connectedSinceMs)},
        {u"bytesSent"_s, static_cast<double>(status.bytesSent)},
        {u"kbps"_s, std::round(status.kbps * 10.0) / 10.0},
        {u"reconnects"_s, static_cast<double>(status.reconnects)},
        {u"retryIn"_s, static_cast<double>(status.retryInMs)},
        {u"title"_s, QString::fromStdString(status.title)},
        {u"sampleRate"_s, static_cast<double>(status.sampleRate)},
    };
    if (status.error) {
        json.insert(u"error"_s, text(stream::streamErrorName(*status.error)));
    }
    return json;
}

UnavailableChoices broadcastUnavailableChoices(const stream::StreamEncoderProbe &probe)
{
    std::map<std::string, std::string> formats;
    for (const auto &[format, id] : {std::pair{stream::StreamFormat::Mp3, blocks::Broadcast::kFormatMp3},
                                     std::pair{stream::StreamFormat::Opus, blocks::Broadcast::kFormatOpus},
                                     std::pair{stream::StreamFormat::Vorbis, blocks::Broadcast::kFormatVorbis},
                                     std::pair{stream::StreamFormat::Aac, blocks::Broadcast::kFormatAac}}) {
        if (const std::optional<std::string_view> reason = stream::streamFormatUnavailable(format, probe)) {
            formats.emplace(std::string{id}, std::string{*reason});
        }
    }
    UnavailableChoices unavailable;
    if (!formats.empty()) {
        unavailable.emplace(std::pair{std::string{blocks::Broadcast::kTypeId}, std::string{"format"}}, formats);
    }
    return unavailable;
}

} // namespace rb::core

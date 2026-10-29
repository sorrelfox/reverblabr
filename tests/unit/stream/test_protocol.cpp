// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What a stream says to a server and how it reads the answer: Icecast's
// PUT and SOURCE, SHOUTcast's password and headers, and the request that
// sets the title.

#include "stream/protocol.h"

#include <gtest/gtest.h>

using namespace rb::stream;

namespace
{

[[nodiscard]] ServerSettings server()
{
    ServerSettings settings;
    settings.host = "radio.example.org";
    settings.port = 8000;
    settings.mount = "/live";
    settings.user = "source";
    settings.password = "hackme";
    settings.userAgent = "Reverblabr/1.0";
    return settings;
}

[[nodiscard]] StationInfo station()
{
    StationInfo info;
    info.name = "Night Shift";
    info.genre = "Ambient";
    info.url = "https://example.org";
    info.description = "Quiet music";
    return info;
}

} // namespace

TEST(StreamProtocolTest, TheIcecastRequestIsAPutWithTheStationInIceHeaders)
{
    AudioInfo audio;
    audio.format = StreamFormat::Mp3;
    audio.bitrateKbps = 192;
    audio.sampleRate = 44100;
    const std::string request = icecastRequest(server(), station(), audio);

    EXPECT_TRUE(request.starts_with("PUT /live HTTP/1.1\r\n")) << request;
    EXPECT_TRUE(request.ends_with("\r\n\r\n"));
    // source:hackme
    EXPECT_NE(request.find("Authorization: Basic c291cmNlOmhhY2ttZQ==\r\n"), std::string::npos) << request;
    EXPECT_NE(request.find("Host: radio.example.org:8000\r\n"), std::string::npos);
    EXPECT_NE(request.find("Content-Type: audio/mpeg\r\n"), std::string::npos);
    EXPECT_NE(request.find("Ice-Name: Night Shift\r\n"), std::string::npos);
    EXPECT_NE(request.find("Ice-Genre: Ambient\r\n"), std::string::npos);
    EXPECT_NE(request.find("Ice-Url: https://example.org\r\n"), std::string::npos);
    EXPECT_NE(request.find("Ice-Description: Quiet music\r\n"), std::string::npos);
    EXPECT_NE(request.find("Ice-Public: 0\r\n"), std::string::npos);
    EXPECT_NE(request.find("Ice-Audio-Info: ice-samplerate=44100;ice-bitrate=192;ice-channels=2\r\n"),
              std::string::npos);
    EXPECT_NE(request.find("User-Agent: Reverblabr/1.0\r\n"), std::string::npos);
}

TEST(StreamProtocolTest, SourceIsTheOldWayOfAskingAndOggIsAudioOgg)
{
    AudioInfo audio;
    audio.format = StreamFormat::Opus;
    const std::string request = icecastRequest(server(), station(), audio, SourceMethod::Source);
    EXPECT_TRUE(request.starts_with("SOURCE /live HTTP/1.0\r\n")) << request;
    EXPECT_NE(request.find("Content-Type: audio/ogg\r\n"), std::string::npos);
    EXPECT_EQ(contentType(StreamFormat::Vorbis), "audio/ogg");
    EXPECT_EQ(contentType(StreamFormat::Aac), "audio/aac");
}

TEST(StreamProtocolTest, AMountPointGetsItsSlashAndEmptyDetailsAreLeftOut)
{
    ServerSettings settings = server();
    settings.mount = "radio.mp3";
    const std::string request = icecastRequest(settings, StationInfo{}, AudioInfo{});
    EXPECT_TRUE(request.starts_with("PUT /radio.mp3 HTTP/1.1\r\n")) << request;
    EXPECT_EQ(request.find("Ice-Name"), std::string::npos);
    EXPECT_EQ(request.find("Ice-Genre"), std::string::npos);
}

TEST(StreamProtocolTest, NothingTypedCanStartAHeaderOfItsOwn)
{
    StationInfo info = station();
    info.name = "Evil\r\nAuthorization: Basic Zm9vOmJhcg==";
    ServerSettings settings = server();
    settings.mount = "/live\r\nX-Injected: 1";
    const std::string request = icecastRequest(settings, info, AudioInfo{});
    EXPECT_EQ(request.find("\r\nAuthorization: Basic Zm9v"), std::string::npos) << request;
    EXPECT_EQ(request.find("\r\nX-Injected"), std::string::npos) << request;
    EXPECT_NE(request.find("Ice-Name: EvilAuthorization: Basic Zm9vOmJhcg==\r\n"), std::string::npos) << request;
}

TEST(StreamProtocolTest, AnIpv6HostIsInBrackets)
{
    ServerSettings settings = server();
    settings.host = "::1";
    EXPECT_NE(icecastRequest(settings, station(), AudioInfo{}).find("Host: [::1]:8000\r\n"), std::string::npos);
}

TEST(StreamProtocolTest, ShoutcastSendsThePasswordThenTheIcyHeaders)
{
    EXPECT_EQ(shoutcastLogin(server()), "hackme\r\n");
    StationInfo info = station();
    info.listed = true;
    AudioInfo audio;
    audio.bitrateKbps = 128;
    const std::string headers = shoutcastHeaders(info, audio);
    EXPECT_NE(headers.find("icy-name:Night Shift\r\n"), std::string::npos) << headers;
    EXPECT_NE(headers.find("icy-genre:Ambient\r\n"), std::string::npos);
    EXPECT_NE(headers.find("icy-url:https://example.org\r\n"), std::string::npos);
    EXPECT_NE(headers.find("icy-pub:1\r\n"), std::string::npos);
    EXPECT_NE(headers.find("icy-br:128\r\n"), std::string::npos);
    EXPECT_NE(headers.find("content-type:audio/mpeg\r\n"), std::string::npos);
    EXPECT_TRUE(headers.ends_with("\r\n\r\n"));
}

TEST(StreamProtocolTest, TheTitleRequestIsIcecastsMetadataWithTheSourcesCredentials)
{
    const std::string request = metadataRequest(server(), "Artist - Song & Co");
    EXPECT_TRUE(request.starts_with(
        "GET /admin/metadata?mount=%2Flive&mode=updinfo&song=Artist%20-%20Song%20%26%20Co&charset=UTF-8 HTTP/1.0\r\n"))
        << request;
    EXPECT_NE(request.find("Authorization: Basic c291cmNlOmhhY2ttZQ==\r\n"), std::string::npos);
}

TEST(StreamProtocolTest, ShoutcastsTitleRequestCarriesThePassword)
{
    ServerSettings settings = server();
    settings.kind = ServerKind::Shoutcast;
    settings.password = "p&ss";
    const std::string request = metadataRequest(settings, "Song");
    EXPECT_TRUE(request.starts_with("GET /admin.cgi?pass=p%26ss&mode=updinfo&song=Song HTTP/1.0\r\n")) << request;
    EXPECT_EQ(request.find("Authorization"), std::string::npos);
}

TEST(StreamProtocolTest, AnHttpAnswerIsReadOnceItsHeadersHaveEnded)
{
    EXPECT_FALSE(parseHttpResponse("HTTP/1.1 200 O").has_value());
    std::optional<Response> ok = parseHttpResponse("HTTP/1.1 200 OK\r\nServer: Icecast 2.4.4\r\n\r\n");
    ASSERT_TRUE(ok.has_value());
    EXPECT_EQ(ok->status, 200);
    EXPECT_EQ(ok->reason, "OK");

    std::optional<Response> refused = parseHttpResponse("HTTP/1.0 401 Authentication Required\r\n\r\n");
    ASSERT_TRUE(refused.has_value());
    EXPECT_EQ(refused->status, 401);
    EXPECT_EQ(errorForStatus(refused->status), StreamError::AuthRejected);
}

TEST(StreamProtocolTest, SomethingThatIsNotHttpIsABadAnswerAtOnce)
{
    std::optional<Response> odd = parseHttpResponse("SSH-2.0-OpenSSH_9.6\r\n");
    ASSERT_TRUE(odd.has_value());
    EXPECT_EQ(odd->status, 0);
    EXPECT_EQ(errorForStatus(odd->status), StreamError::BadResponse);
    // Cut short by the server closing, it is read as far as it goes.
    std::optional<Response> cut = parseHttpResponse("HTTP/1.0 403 Mountpoint in use", true);
    ASSERT_TRUE(cut.has_value());
    EXPECT_EQ(cut->status, 403);
    EXPECT_EQ(errorForStatus(cut->status), StreamError::MountInUse);
}

TEST(StreamProtocolTest, ShoutcastsAnswerIsOk2OrAWrongPassword)
{
    EXPECT_FALSE(parseShoutcastResponse("OK").has_value());
    std::optional<Response> ok = parseShoutcastResponse("OK2\r\nicy-caps:11\r\n\r\n");
    ASSERT_TRUE(ok.has_value());
    EXPECT_FALSE(errorForStatus(ok->status).has_value());
    std::optional<Response> wrong = parseShoutcastResponse("invalid password\r\n");
    ASSERT_TRUE(wrong.has_value());
    EXPECT_EQ(errorForStatus(wrong->status), StreamError::AuthRejected);
}

TEST(StreamProtocolTest, OnlyWhatWaitingCanMendIsTriedAgain)
{
    EXPECT_FALSE(isWorthRetrying(StreamError::AuthRejected));
    EXPECT_FALSE(isWorthRetrying(StreamError::NoPassword));
    EXPECT_FALSE(isWorthRetrying(StreamError::EncoderMissing));
    EXPECT_FALSE(isWorthRetrying(StreamError::NotConfigured));
    EXPECT_TRUE(isWorthRetrying(StreamError::Closed));
    EXPECT_TRUE(isWorthRetrying(StreamError::ConnectFailed));
    EXPECT_TRUE(isWorthRetrying(StreamError::MountInUse));
    EXPECT_TRUE(isWorthRetrying(StreamError::TimedOut));
    EXPECT_EQ(streamErrorName(StreamError::MountInUse), "mountInUse");
}

TEST(StreamProtocolTest, Base64AndPercentEncodingAreTheStandardOnes)
{
    EXPECT_EQ(base64(""), "");
    EXPECT_EQ(base64("f"), "Zg==");
    EXPECT_EQ(base64("fo"), "Zm8=");
    EXPECT_EQ(base64("foo"), "Zm9v");
    EXPECT_EQ(base64("foobar"), "Zm9vYmFy");
    EXPECT_EQ(percentEncoded("a b/ü~"), "a%20b%2F%C3%BC~");
}

// **What a person typed goes out as it is.** A name or a password with
// accents in it is not cut short: only something longer than a server keeps
// is, and then at the start of a character.
TEST(StreamProtocolTest, TextWithAccentsIsSentWhole)
{
    ServerSettings settings = server();
    settings.user = "josé";
    settings.password = "pässwörd";
    StationInfo info = station();
    info.name = "Radio Café";
    const std::string request = icecastRequest(settings, info, AudioInfo{});
    EXPECT_NE(request.find("Ice-Name: Radio Café\r\n"), std::string::npos) << request;
    EXPECT_NE(request.find("Authorization: Basic " + base64("josé:pässwörd") + "\r\n"), std::string::npos) << request;

    settings.kind = ServerKind::Shoutcast;
    settings.password = "josé";
    EXPECT_EQ(shoutcastLogin(settings), "josé\r\n");
    settings.password = "two\r\nlines";
    EXPECT_EQ(shoutcastLogin(settings), "twolines\r\n") << "a line end inside the password line";

    // Past what a server keeps, cut where a character starts.
    std::string longName;
    for (int index = 0; index < 400; ++index) {
        longName += "é";
    }
    info.name = longName;
    const std::string cut = icecastRequest(server(), info, AudioInfo{});
    const std::size_t from = cut.find("Ice-Name: ") + 10;
    const std::string name = cut.substr(from, cut.find("\r\n", from) - from);
    EXPECT_LE(name.size(), 512U);
    EXPECT_EQ(name.size() % 2, 0U) << "a character was cut in half";
}

TEST(StreamProtocolTest, ShoutcastsTitleRequestCallsItselfABrowser)
{
    ServerSettings settings = server();
    settings.kind = ServerKind::Shoutcast;
    EXPECT_NE(metadataRequest(settings, "Song").find("User-Agent: Reverblabr/1.0 (Mozilla compatible)\r\n"),
              std::string::npos);
    EXPECT_NE(metadataRequest(server(), "Song").find("User-Agent: Reverblabr/1.0\r\n"), std::string::npos);
}

TEST(StreamProtocolTest, OnlyA2xxLetsAStreamGoAhead)
{
    EXPECT_FALSE(errorForStatus(200).has_value());
    EXPECT_TRUE(errorForStatus(100).has_value());
    EXPECT_TRUE(errorForStatus(101).has_value());
    EXPECT_TRUE(isWorthRetrying(*errorForStatus(100)));
}

TEST(StreamProtocolTest, AShoutcastHangUpIsNotAWrongPassword)
{
    std::optional<Response> silent = parseShoutcastResponse("", true);
    ASSERT_TRUE(silent.has_value());
    EXPECT_NE(errorForStatus(silent->status), StreamError::AuthRejected);
    EXPECT_TRUE(isWorthRetrying(*errorForStatus(silent->status)));
}

TEST(StreamProtocolTest, SettingsTheEncoderRefusesAndAStoreThatWouldNotOpenAreNotRetried)
{
    EXPECT_FALSE(isWorthRetrying(StreamError::UnsupportedSettings));
    EXPECT_FALSE(isWorthRetrying(StreamError::PasswordUnavailable));
    EXPECT_EQ(streamErrorName(StreamError::UnsupportedSettings), "unsupportedSettings");
    EXPECT_EQ(streamErrorName(StreamError::PasswordUnavailable), "passwordUnavailable");
}

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "stream/protocol.h"

#include "stream/text.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>

namespace rb::stream
{

namespace
{

/// \a text fit to go in a header: control characters out, so that nothing
/// in it ends the line, and no longer than a server keeps.
[[nodiscard]] std::string headerValue(std::string_view text)
{
    constexpr std::size_t kMaxBytes = 512;
    std::string out;
    out.reserve(std::min(text.size(), kMaxBytes + 4));
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 || byte == 0x7f) {
            continue;
        }
        out.push_back(c);
    }
    text::cutUtf8(out, kMaxBytes);
    return out;
}

/// The mount point with one leading slash, whatever was typed.
[[nodiscard]] std::string mountPath(std::string_view mount)
{
    std::string path = headerValue(mount);
    std::erase(path, ' ');
    if (path.empty() || path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    return path;
}

/// "host" or "host:port", and an IPv6 address in brackets, as a Host
/// header has it.
[[nodiscard]] std::string hostHeader(const ServerSettings &server)
{
    const std::string host = headerValue(server.host);
    const bool ipv6 = host.find(':') != std::string::npos;
    return std::format("{}{}{}:{}", ipv6 ? "[" : "", host, ipv6 ? "]" : "", server.port);
}

[[nodiscard]] std::string basicAuthorization(const ServerSettings &server)
{
    return "Basic " + base64(std::format("{}:{}", headerValue(server.user), server.password));
}

[[nodiscard]] std::string_view trimmed(std::string_view value)
{
    return text::trimmed(value);
}

[[nodiscard]] std::string lowered(std::string_view value)
{
    std::string out(value);
    std::ranges::transform(out, out.begin(), [](char c) { return text::lower(c); });
    return out;
}

} // namespace

std::string_view contentType(StreamFormat format) noexcept
{
    switch (format) {
    case StreamFormat::Mp3:
        return "audio/mpeg";
    case StreamFormat::Opus:
    case StreamFormat::Vorbis:
        return "audio/ogg";
    case StreamFormat::Aac:
        return "audio/aac";
    }
    return "application/octet-stream";
}

std::string
icecastRequest(const ServerSettings &server, const StationInfo &station, const AudioInfo &audio, SourceMethod method)
{
    // **HTTP/1.1 for PUT and 1.0 for SOURCE**, which is what each version of
    // Icecast that takes them expects; neither has a length, because the
    // body is the stream and goes on until the source stops.
    std::string request = method == SourceMethod::Put ? std::format("PUT {} HTTP/1.1\r\n", mountPath(server.mount))
                                                      : std::format("SOURCE {} HTTP/1.0\r\n", mountPath(server.mount));
    request += std::format("Host: {}\r\n", hostHeader(server));
    request += std::format("Authorization: {}\r\n", basicAuthorization(server));
    request += std::format("User-Agent: {}\r\n", headerValue(server.userAgent));
    request += std::format("Content-Type: {}\r\n", contentType(audio.format));
    request += std::format("Ice-Public: {}\r\n", station.listed ? 1 : 0);
    for (const auto &[name, value] : {std::pair{"Ice-Name", &station.name},
                                      std::pair{"Ice-Description", &station.description},
                                      std::pair{"Ice-Genre", &station.genre},
                                      std::pair{"Ice-Url", &station.url}}) {
        if (const std::string text = headerValue(*value); !text.empty()) {
            request += std::format("{}: {}\r\n", name, text);
        }
    }
    request += std::format("Ice-Audio-Info: ice-samplerate={};ice-bitrate={};ice-channels={}\r\n",
                           audio.sampleRate,
                           audio.bitrateKbps,
                           audio.channels);
    request += "\r\n";
    return request;
}

std::string shoutcastLogin(const ServerSettings &server)
{
    // **The password as it is**: nothing cut, nothing dropped. Only the line
    // ends go, which the protocol cannot carry inside a line anyway.
    std::string password = server.password;
    std::erase_if(password, [](char c) { return c == '\r' || c == '\n'; });
    return password + "\r\n";
}

std::string shoutcastHeaders(const StationInfo &station, const AudioInfo &audio)
{
    std::string headers;
    for (const auto &[name, value] : {std::pair{"icy-name", &station.name},
                                      std::pair{"icy-genre", &station.genre},
                                      std::pair{"icy-url", &station.url},
                                      std::pair{"icy-description", &station.description}}) {
        if (const std::string text = headerValue(*value); !text.empty()) {
            headers += std::format("{}:{}\r\n", name, text);
        }
    }
    headers += std::format("icy-pub:{}\r\n", station.listed ? 1 : 0);
    headers += std::format("icy-br:{}\r\n", audio.bitrateKbps);
    headers += std::format("content-type:{}\r\n", contentType(audio.format));
    headers += "\r\n";
    return headers;
}

std::string metadataRequest(const ServerSettings &server, std::string_view title)
{
    const std::string song = percentEncoded(title);
    std::string request;
    if (server.kind == ServerKind::Icecast) {
        request = std::format("GET /admin/metadata?mount={}&mode=updinfo&song={}&charset=UTF-8 HTTP/1.0\r\n",
                              percentEncoded(mountPath(server.mount)),
                              song);
        request += std::format("Authorization: {}\r\n", basicAuthorization(server));
    } else {
        request = std::format(
            "GET /admin.cgi?pass={}&mode=updinfo&song={} HTTP/1.0\r\n", percentEncoded(server.password), song);
    }
    request += std::format("Host: {}\r\n", hostHeader(server));
    // SHOUTcast 1's admin.cgi answers only what calls itself a browser.
    request += std::format("User-Agent: {}{}\r\n",
                           headerValue(server.userAgent),
                           server.kind == ServerKind::Shoutcast ? " (Mozilla compatible)" : "");
    request += "\r\n";
    return request;
}

std::string_view streamErrorName(StreamError error) noexcept
{
    switch (error) {
    case StreamError::NotConfigured:
        return "notConfigured";
    case StreamError::NoPassword:
        return "noPassword";
    case StreamError::ResolveFailed:
        return "resolveFailed";
    case StreamError::ConnectFailed:
        return "connectFailed";
    case StreamError::TimedOut:
        return "timedOut";
    case StreamError::Closed:
        return "closed";
    case StreamError::AuthRejected:
        return "authRejected";
    case StreamError::MountInUse:
        return "mountInUse";
    case StreamError::Rejected:
        return "rejected";
    case StreamError::BadResponse:
        return "badResponse";
    case StreamError::EncoderMissing:
        return "encoderMissing";
    case StreamError::EncoderFailed:
        return "encoderFailed";
    case StreamError::UnsupportedSettings:
        return "unsupportedSettings";
    case StreamError::PasswordUnavailable:
        return "passwordUnavailable";
    }
    return "rejected";
}

bool isWorthRetrying(StreamError error) noexcept
{
    switch (error) {
    case StreamError::NotConfigured:
    case StreamError::NoPassword:
    case StreamError::AuthRejected:
    case StreamError::EncoderMissing:
    case StreamError::UnsupportedSettings:
    case StreamError::PasswordUnavailable:
        return false;
    case StreamError::ResolveFailed:
    case StreamError::ConnectFailed:
    case StreamError::TimedOut:
    case StreamError::Closed:
    case StreamError::MountInUse:
    case StreamError::Rejected:
    case StreamError::BadResponse:
    case StreamError::EncoderFailed:
        return true;
    }
    return true;
}

std::optional<Response> parseHttpResponse(std::string_view received, bool closed)
{
    const std::size_t end = received.find("\r\n\r\n");
    if (end == std::string_view::npos && !closed) {
        // A server that is not HTTP at all is told apart before it has
        // sent a whole answer, which it may never do.
        constexpr std::string_view kStart = "HTTP/";
        if (received.size() >= kStart.size() && !received.starts_with(kStart)) {
            return Response{0, std::string(trimmed(received.substr(0, received.find('\n'))))};
        }
        return std::nullopt;
    }
    std::string_view line = received.substr(0, received.find("\r\n"));
    if (!line.starts_with("HTTP/")) {
        return Response{0, std::string(trimmed(line))};
    }
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
        return Response{0, std::string(line)};
    }
    line.remove_prefix(space + 1);
    int status = 0;
    const auto [rest, error] = std::from_chars(line.data(), line.data() + line.size(), status);
    if (error != std::errc{}) {
        return Response{0, std::string(line)};
    }
    return Response{status, std::string(trimmed(std::string_view(rest, line.data() + line.size())))};
}

std::optional<Response> parseShoutcastResponse(std::string_view received, bool closed)
{
    const std::size_t end = received.find('\n');
    if (end == std::string_view::npos && !closed) {
        return std::nullopt;
    }
    const std::string first = lowered(trimmed(received.substr(0, end)));
    if (first.starts_with("ok")) {
        return Response{200, first};
    }
    // SHOUTcast says "invalid password", and so does Icecast's SHOUTcast
    // port. **A server that hangs up without a word has said nothing about
    // the password**: it may be restarting, so that is tried again.
    if (first.find("password") != std::string::npos) {
        return Response{401, first};
    }
    return Response{0, first};
}

std::optional<StreamError> errorForStatus(int status) noexcept
{
    // **Only a 2xx lets a stream go ahead.** A source sends no Expect, so a
    // 1xx is not an answer to anything it asked.
    if (status >= 200 && status < 300) {
        return std::nullopt;
    }
    if (status == 401) {
        return StreamError::AuthRejected;
    }
    // Icecast says 403 for a mount point that is taken, and for a server
    // that has as many sources as it takes.
    if (status == 403) {
        return StreamError::MountInUse;
    }
    if (status == 0) {
        return StreamError::BadResponse;
    }
    return StreamError::Rejected;
}

std::string percentEncoded(std::string_view text)
{
    constexpr std::string_view kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3);
    for (const char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (text::isAlnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            out.push_back('%');
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0f]);
        }
    }
    return out;
}

std::string base64(std::string_view bytes)
{
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t index = 0;
    for (; index + 3 <= bytes.size(); index += 3) {
        const std::uint32_t triple = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16
            | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1])) << 8
            | static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 2]));
        for (const int shift : {18, 12, 6, 0}) {
            out.push_back(kAlphabet[(triple >> shift) & 0x3f]);
        }
    }
    const std::size_t left = bytes.size() - index;
    if (left > 0) {
        std::uint32_t triple = static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index])) << 16;
        if (left == 2) {
            triple |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[index + 1])) << 8;
        }
        out.push_back(kAlphabet[(triple >> 18) & 0x3f]);
        out.push_back(kAlphabet[(triple >> 12) & 0x3f]);
        out.push_back(left == 2 ? kAlphabet[(triple >> 6) & 0x3f] : '=');
        out.push_back('=');
    }
    return out;
}

} // namespace rb::stream

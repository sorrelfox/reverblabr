// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace rb::stream
{

/// What kind of server a stream goes to, which decides how it says hello.
enum class ServerKind : std::uint8_t {
    /// Icecast 2: an HTTP request with the source's user and password, the
    /// station in `Ice-` headers, and the audio as the request's body.
    Icecast,
    /// SHOUTcast's own protocol, which Icecast speaks too: the password
    /// alone on the port after the listeners' one, then `icy-` headers.
    Shoutcast,
};

/// The formats a stream is sent in, each through the system's FFmpeg.
enum class StreamFormat : std::uint8_t {
    Mp3,
    Opus, ///< In Ogg.
    Vorbis, ///< In Ogg.
    Aac, ///< ADTS frames, which a player can join at any frame.
};

/// Whether \a format travels in Ogg pages, whose comment header is where
/// the title goes, rather than in frames a server titles on request.
[[nodiscard]] constexpr bool isOgg(StreamFormat format) noexcept
{
    return format == StreamFormat::Opus || format == StreamFormat::Vorbis;
}

/// The MIME type a server is told the stream is, and passes on to listeners.
[[nodiscard]] std::string_view contentType(StreamFormat format) noexcept;

/// Where a stream goes, and who it says it is. **The password is never
/// kept with the rest**: it comes from the desktop's secret store when the
/// stream starts, and lives only in memory.
struct ServerSettings {
    ServerKind kind = ServerKind::Icecast;
    std::string host;
    std::uint16_t port = 8000;
    /// Icecast's mount point, with its leading slash. SHOUTcast has none.
    std::string mount = "/stream";
    /// Icecast's source user; SHOUTcast has no user.
    std::string user = "source";
    std::string password;
    /// The program and its version, as the server logs it.
    std::string userAgent = "Reverblabr";

    [[nodiscard]] bool operator==(const ServerSettings &other) const = default;
};

/// What a server is told about the station, for its directory listing and
/// the players that show it.
struct StationInfo {
    std::string name;
    std::string genre;
    std::string url;
    std::string description;
    /// Whether the server may list the station in a public directory.
    bool listed = false;

    [[nodiscard]] bool operator==(const StationInfo &other) const = default;
};

/// What the audio is, for the server's `ice-audio-info` and `icy-br`.
struct AudioInfo {
    StreamFormat format = StreamFormat::Mp3;
    std::uint32_t bitrateKbps = 128;
    std::uint32_t sampleRate = 48000;
    std::uint32_t channels = 2;
};

/// The two ways of asking Icecast to take a stream: `PUT` since 2.4, and
/// `SOURCE` before it, which 2.4 still takes.
enum class SourceMethod : std::uint8_t {
    Put,
    Source,
};

/// The request that starts an Icecast stream, headers and all, up to and
/// including the blank line; the audio follows it.
///
/// **Nothing a person typed can start a header of its own**: line breaks
/// and other control characters in the station's details are taken out,
/// so a station called "x\r\nAuthorization: …" is a station with an odd
/// name rather than a second header.
[[nodiscard]] std::string icecastRequest(const ServerSettings &server,
                                         const StationInfo &station,
                                         const AudioInfo &audio,
                                         SourceMethod method = SourceMethod::Put);

/// What a SHOUTcast source sends first: its password, on a line of its own.
[[nodiscard]] std::string shoutcastLogin(const ServerSettings &server);

/// What it sends once the server has taken the password: the station, the
/// rate and the type, then a blank line, and then the audio.
[[nodiscard]] std::string shoutcastHeaders(const StationInfo &station, const AudioInfo &audio);

/// The request that sets the title listeners see, on a connection of its
/// own while the stream goes on: Icecast's `/admin/metadata` with the
/// source's user and password, or SHOUTcast's `/admin.cgi` with the
/// password in it. **Only for MP3 and AAC**; an Ogg stream carries its
/// title in its own comment header.
[[nodiscard]] std::string metadataRequest(const ServerSettings &server, std::string_view title);

/// Why a stream is not going, or stopped.
enum class StreamError : std::uint8_t {
    /// No host, or no port.
    NotConfigured,
    /// The secret store has no password for it.
    NoPassword,
    /// The host's name could not be looked up.
    ResolveFailed,
    /// Nothing answered at the host and port, or something refused.
    ConnectFailed,
    /// The server took too long to answer, or to take the audio.
    TimedOut,
    /// The server closed the connection.
    Closed,
    /// The server would not take the user and password.
    AuthRejected,
    /// The server has a stream at that mount point already, or will not
    /// take another source.
    MountInUse,
    /// The server said no for some other reason.
    Rejected,
    /// The server answered with something that is not a server's answer.
    BadResponse,
    /// The system's FFmpeg has no encoder for the format.
    EncoderMissing,
    /// The encoder stopped in the middle of a stream.
    EncoderFailed,
    /// The encoder would not start with these settings: a bit rate the
    /// format does not take for this many channels, say. Found before the
    /// server is asked, and not tried again until the settings change.
    UnsupportedSettings,
    /// The secret store could not be asked for the password: locked, not
    /// there, or the person said no. Not tried again until the stream is
    /// started again or given a password.
    PasswordUnavailable,
};

/// The name a status carries, "authRejected" and the like.
[[nodiscard]] std::string_view streamErrorName(StreamError error) noexcept;

/// **Whether trying again later can help.** A wrong password stays wrong
/// however long one waits, and an encoder that is missing stays missing;
/// a server that restarted, a network that dropped or a mount point still
/// held by the stream that was there before is worth another try.
[[nodiscard]] bool isWorthRetrying(StreamError error) noexcept;

/// What a server answered, once a whole answer has arrived.
struct Response {
    /// The status: an HTTP one, or 200 for SHOUTcast's "OK2".
    int status = 0;
    /// The rest of the status line, for the log.
    std::string reason;
};

/// Reads a server's answer to an Icecast request from what has arrived so
/// far: nothing until the blank line that ends its headers, and an error
/// for something that is not HTTP. An answer cut short by the server
/// closing is read as far as it goes, by \a closed.
[[nodiscard]] std::optional<Response> parseHttpResponse(std::string_view received, bool closed = false);

/// The same for SHOUTcast's answer to a password, which is "OK2" and some
/// capabilities, or a line saying the password is wrong.
[[nodiscard]] std::optional<Response> parseShoutcastResponse(std::string_view received, bool closed = false);

/// What an answer means for the stream: nothing for one that lets it go
/// ahead.
[[nodiscard]] std::optional<StreamError> errorForStatus(int status) noexcept;

/// \a text with every byte but the unreserved ones percent-encoded, for a
/// query string.
[[nodiscard]] std::string percentEncoded(std::string_view text);

/// \a bytes in base64, for HTTP's Basic authentication.
[[nodiscard]] std::string base64(std::string_view bytes);

} // namespace rb::stream

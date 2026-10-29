// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/backoff.h"
#include "stream/protocol.h"
#include "stream/title.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace rb::rt
{
class AudioTap;
} // namespace rb::rt

namespace rb::stream
{

/// Everything a stream is made from but its password: read off the block
/// whenever one of its settings changes.
struct BroadcastSettings {
    ServerSettings server;
    StationInfo station;
    StreamFormat format = StreamFormat::Mp3;
    std::uint32_t bitrateKbps = 128;
    /// One channel, the two sides mixed, rather than two.
    bool mono = false;
    /// The title listeners see, with the tokens renderTitle() fills in.
    std::string titleTemplate = "%artist - %title";
    std::string sessionName;
    /// Where the title comes from when nothing hands one in: a text file,
    /// looked at once a second. Empty for none.
    std::filesystem::path nowPlayingFile;

    [[nodiscard]] bool operator==(const BroadcastSettings &other) const = default;
};

/// Where a stream stands, as its status says it.
struct BroadcastStatus {
    enum class State : std::uint8_t {
        /// Not started, or stopped.
        Idle,
        /// Waiting for its password from the secret store.
        WaitingForPassword,
        Connecting,
        /// On the air.
        Connected,
        /// Lost the server, or could not reach it, and will try again in
        /// retryInMs.
        Retrying,
        /// Gave up: trying again cannot help until something is changed
        /// (error says what is wrong).
        Failed,
    };

    State state = State::Idle;
    /// Why it is retrying or has failed, or why it last lost the server.
    std::optional<StreamError> error;
    /// When the connection that is up now was made, in milliseconds since
    /// the epoch; zero when there is none.
    std::int64_t connectedSinceMs = 0;
    /// Bytes sent to the server since the stream was started, across every
    /// connection.
    std::uint64_t bytesSent = 0;
    /// The rate the bytes have been going out at over the last few
    /// seconds, in kilobits a second.
    double kbps = 0.0;
    /// How many times it connected again after losing the server.
    std::uint32_t reconnects = 0;
    /// How long until the next try, while retrying.
    std::int64_t retryInMs = 0;
    /// The title the server was last given.
    std::string title;
    /// The rate the stream goes out at, once connected.
    std::uint32_t sampleRate = 0;

    [[nodiscard]] bool operator==(const BroadcastStatus &other) const = default;
};

/// The name a status carries for \a state, "connected" and the like.
[[nodiscard]] std::string_view broadcastStateName(BroadcastStatus::State state) noexcept;

struct BroadcastOptions {
    /// How long the stream sleeps when there is nothing to do. The tap holds
    /// seconds, so this only decides how soon audio reaches the server.
    std::chrono::milliseconds pollInterval{20};
    /// How long connecting, and the server's answer, may take.
    std::chrono::milliseconds connectTimeout{10000};
    /// How long a stream waits for the session to say its rate before it
    /// connects at 48 kHz without knowing it.
    std::chrono::milliseconds rateWait{2000};
    /// How long the server may refuse to take any audio before the
    /// connection is given up as lost.
    std::chrono::milliseconds sendTimeout{10000};
    /// How often the status is said while nothing but the numbers changes.
    std::chrono::milliseconds statusInterval{1000};
    Backoff::Options backoff;
    /// What time it is, for connectedSinceMs. Empty asks the system.
    std::function<std::int64_t()> wallClockMs;
};

/// **The thread that puts a Broadcast block's audio on the air.**
///
/// The block copies each cycle into its tap and goes back to the audio;
/// this drains the tap, encodes what it finds and sends it to the server,
/// and may wait for the network as long as it has to. One stream for each
/// Broadcast block of a running session, each with a thread of its own, so
/// that a server that is slow to answer holds up nobody else.
///
/// **It keeps trying.** A server that goes away, a network that drops, a
/// mount point that is still held are tried again after a wait that grows
/// (Backoff); the audio that arrives meanwhile is let go, so the stream
/// comes back with the sound as it is then and not with what is minutes
/// old. A wrong password, or a format this FFmpeg cannot make, stops it
/// until something is changed.
///
/// **Its title comes from outside.** setNowPlaying() is what a player that
/// knows what it is playing calls; the text file in the settings is looked
/// at only while nothing has been handed in that way. Either way the title
/// template turns the track into what listeners see, and a new title goes
/// to the server as soon as it is known: as Icecast's metadata request for
/// MP3 and AAC, and as a new chained stream for Ogg.
///
/// Status is delivered to the sink on the stream's own thread, when it
/// changes and once a statusInterval while connected.
class BroadcastStream
{
public:
    using StatusSink = std::function<void(const BroadcastStatus &)>;

    BroadcastStream(std::shared_ptr<rt::AudioTap> tap, StatusSink sink, BroadcastOptions options = {});
    /// Closes the connection and stops the thread.
    ~BroadcastStream();

    BroadcastStream(const BroadcastStream &) = delete;
    BroadcastStream &operator=(const BroadcastStream &) = delete;

    /// What to stream, and where. **A change that a server has to be told
    /// in its first request** (where it goes, the format, the station)
    /// connects again; one to the title only gives it the new title.
    void configure(BroadcastSettings settings);

    /// The password, from the secret store: nothing when it has none, and
    /// the stream says so and waits for one. A new password after a
    /// refused one tries again at once.
    void setPassword(std::optional<std::string> password);

    /// The secret store could not be asked: locked, not there, or refused.
    /// The stream says so (StreamError::PasswordUnavailable) and waits,
    /// until it is given a password.
    void setPasswordUnavailable();

    /// **What is playing**, from whatever knows. Takes over from the
    /// now-playing file until the stream is configured with another one.
    void setNowPlaying(Track track);

    /// The status as it was last said.
    [[nodiscard]] BroadcastStatus status() const;

    /// **Tells the thread to stop, and does not wait for it.** A caller
    /// letting go of several streams tells them all first and then lets
    /// go, so they wind down together rather than one after another.
    void requestStop();

private:
    struct Shared;
    void run();

    std::unique_ptr<Shared> m_shared;
    std::thread m_thread;
};

} // namespace rb::stream

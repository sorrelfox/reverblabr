// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "stream/backoff.h"
#include "stream/broadcaststream.h"
#include "stream/compositor.h"
#include "stream/outputstream.h"
#include "stream/videoencoder.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace rb::rt
{
class AudioTap;
} // namespace rb::rt

namespace rb::stream
{

/// The size, the rate and the bit rates a live stream goes out at.
struct LiveQuality {
    int width = 1280;
    int height = 720;
    int fps = 30;
    std::uint32_t videoKbps = 2500;
    std::uint32_t audioKbps = 128;

    [[nodiscard]] bool operator==(const LiveQuality &other) const = default;
};

/// The three a person chooses from.
enum class LiveQualityPreset : std::uint8_t {
    /// 1024 × 576 at 15 frames a second: a slow upload, or a phone's.
    Good,
    /// 1280 × 720 at 30.
    Higher,
    /// 1920 × 1080 at 30.
    Best,
};

/// **What each preset is.** The pictures are words and a moving line over
/// a still background, which H.264 makes small; the bit rates are what
/// that needs and what services ask for at each size, well below what
/// they allow.
[[nodiscard]] LiveQuality liveQuality(LiveQualityPreset preset) noexcept;

/// Everything a live stream is made from but its key: read off the block
/// whenever one of its settings changes.
struct LiveStreamSettings {
    /// The service's address, `rtmp://…` or `rtmps://…` (parseRtmpUrl).
    std::string url;
    LiveQuality quality;
    /// The H.264 encoder asked for; nothing for whichever works best here.
    std::optional<VideoEncoderKind> encoder;
    /// The picture: the scene's track is filled in by the stream, from what
    /// is playing and the template.
    Scene scene;
    /// What is playing, as the picture says it, with renderTitle()'s tokens.
    std::string titleTemplate = "%artist - %title";
    std::string sessionName;
    /// Where the track comes from when nothing hands one in: a text file,
    /// looked at once a second. Empty for none.
    std::filesystem::path nowPlayingFile;

    [[nodiscard]] bool operator==(const LiveStreamSettings &other) const = default;
};

/// Where a live stream stands: what a radio stream's status says (the
/// state, the error, the bytes and the reconnects), and the video's.
struct LiveStreamStatus {
    BroadcastStatus stream;
    /// The encoder in use while connected ("x264" and the like); empty
    /// otherwise.
    std::string encoder;
    int width = 0;
    int height = 0;
    int fps = 0;
    /// Frames sent since the stream was started, and frames left out
    /// because the stream had fallen behind the sound.
    std::uint64_t framesSent = 0;
    std::uint64_t framesDropped = 0;

    [[nodiscard]] bool operator==(const LiveStreamStatus &other) const = default;
};

struct LiveStreamOptions {
    /// How long the stream sleeps when there is nothing to do: well under a
    /// frame, so frames go out evenly.
    std::chrono::milliseconds pollInterval{10};
    /// How long connecting, the handshakes and the stream's start may take.
    std::chrono::milliseconds connectTimeout{10000};
    /// How long a stream waits for the session to say its rate before it
    /// connects at 48 kHz without knowing it.
    std::chrono::milliseconds rateWait{2000};
    /// How long the network may refuse to take anything before the
    /// connection is given up as lost.
    std::chrono::milliseconds sendTimeout{10000};
    /// How often the status is said while nothing but the numbers changes.
    std::chrono::milliseconds statusInterval{1000};
    Backoff::Options backoff;
    /// What time it is, for connectedSinceMs. Empty asks the system.
    std::function<std::int64_t()> wallClockMs;
    /// The authorities an RTMPS server is checked against; empty for the
    /// system's.
    std::string caFile;
    /// Which encoders there are; empty for the system's probe. A test
    /// leaves some out to see the next one taken.
    std::optional<VideoEncoderProbe> probe;
    /// Handed every frame as it is drawn, on the stream's thread, before it
    /// is encoded. For a test to look at the picture; empty otherwise.
    std::function<void(const Canvas &)> onFrame;
};

/// **The thread that puts a Live Stream block on a video service**: the
/// session's sound, with a picture made for it, over RTMP or RTMPS.
///
/// The block copies each cycle into its tap and goes back to the audio, as
/// a Broadcast block does; this drains the tap, measures what it finds for
/// the picture, encodes it as AAC, and **draws and encodes a frame each
/// time the sound has moved on by one**, so the picture keeps time with
/// the sound however long the stream runs: there is one clock, and it is
/// the audio's. A stream that falls behind (a slow encoder on a busy
/// computer) leaves frames out rather than letting the sound wait.
///
/// The encoders are made before the server is asked, the first H.264
/// encoder that works of the ones there are; a server that goes away is
/// tried again after a wait that grows, as a radio stream's is; and a
/// missing key or encoder stops it until something is changed.
///
/// Status is delivered to the sink on the stream's own thread, when it
/// changes and once a statusInterval while connected.
class LiveStream final : public OutputStream
{
public:
    using StatusSink = std::function<void(const LiveStreamStatus &)>;

    LiveStream(std::shared_ptr<rt::AudioTap> tap, StatusSink sink, LiveStreamOptions options = {});
    /// Ends the stream and stops the thread.
    ~LiveStream() override;

    LiveStream(const LiveStream &) = delete;
    LiveStream &operator=(const LiveStream &) = delete;

    /// What to stream, and where. A change of address, quality or encoder
    /// connects again; one to the picture only redraws it.
    void configure(LiveStreamSettings settings);

    void setPassword(std::optional<std::string> key) override;
    void setPasswordUnavailable() override;
    void setNowPlaying(Track track) override;
    void requestStop() override;

    /// The status as it was last said.
    [[nodiscard]] LiveStreamStatus status() const;

private:
    struct Shared;
    void run();

    std::unique_ptr<Shared> m_shared;
    std::thread m_thread;
};

} // namespace rb::stream

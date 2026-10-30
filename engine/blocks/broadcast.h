// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::rt
{
class AudioTap;
} // namespace rb::rt

namespace rb::blocks
{

/// Sends what arrives at it to an internet radio server, and passes it on
/// untouched.
///
/// **The block is the audio thread's half, and nothing more**, as the
/// Recorder is. It copies every cycle into a tap (PrepareSpec::recording)
/// and carries on; encoding and the network are a stream's business, on a
/// thread of their own that may wait for a server as long as it likes
/// (`stream::BroadcastStream`, driven by the session runtime). The settings
/// are kept here because they belong to the block in the session; the
/// stream reads them whenever one changes.
///
/// **The password is not one of them.** It is kept in the desktop's secret
/// store, by the engine, and never in the session file (ADR-0026).
class Broadcast final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "output.broadcast";

    /// The kinds of server, by the stable ids a session file holds.
    static constexpr std::string_view kServerIcecast = "icecast";
    static constexpr std::string_view kServerShoutcast = "shoutcast";

    /// The formats.
    static constexpr std::string_view kFormatMp3 = "mp3";
    static constexpr std::string_view kFormatOpus = "opus";
    static constexpr std::string_view kFormatVorbis = "vorbis";
    static constexpr std::string_view kFormatAac = "aac";

    static constexpr std::string_view kChannelsStereo = "stereo";
    static constexpr std::string_view kChannelsMono = "mono";

    /// Where the title comes from: nowhere but the template, a text file,
    /// or a media player on the session bus, which the engine follows and
    /// hands the stream a track from through
    /// BroadcastStream::setNowPlaying().
    static constexpr std::string_view kNowPlayingNone = "none";
    static constexpr std::string_view kNowPlayingFile = "file";
    static constexpr std::string_view kNowPlayingPlayer = "player";

    /// The three, as the nowPlaying setting offers them. **Shared with
    /// every block whose title follows what is playing** (Live Stream), so
    /// a new source is offered by all of them at once, and the engine
    /// follows a player for any of them alike.
    [[nodiscard]] static std::vector<std::string> nowPlayingChoices();

    /// What listeners see when the template is left as it is.
    static constexpr std::string_view kDefaultTitleTemplate = "%artist - %title";

    enum Param : std::uint32_t {
        Server = 0,
        Host,
        Port,
        /// Whether the stream goes over TLS. Icecast only.
        Secure,
        /// Icecast's mount point; SHOUTcast has none.
        Mount,
        /// Icecast's source user; SHOUTcast has none.
        User,
        Format,
        /// In kilobits a second.
        Bitrate,
        Channels,
        /// What the server is told about the station.
        StationName,
        Genre,
        Url,
        Description,
        /// Whether the server may list the station in a public directory.
        Listed,
        /// The title listeners see, with stream::renderTitle()'s tokens.
        TitleTemplate,
        NowPlaying,
        /// The text file the title is read from, for NowPlaying "file".
        NowPlayingFile,
        /// The media player the title follows, for NowPlaying "player": a
        /// player key (mpris::playerKey()), or empty for the one the
        /// session captures.
        NowPlayingPlayer,
    };

    Broadcast();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::AudioTap *m_tap = nullptr;
    std::uint32_t m_sampleRate = 0;
};

} // namespace rb::blocks

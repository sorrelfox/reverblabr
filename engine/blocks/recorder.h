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

/// Writes what arrives at it to a file, and passes it on untouched.
///
/// **The block is the audio thread's half, and nothing more.** It copies
/// every cycle into a tap (PrepareSpec::recording) and carries on; the file
/// is somebody else's business, on a thread that may wait for the disk as
/// long as it likes (`record::RecordingWriter`, driven by the session
/// runtime). So nothing here opens a file, names one or knows what format
/// it is in: those settings are kept here because they belong to the block
/// in the session, and the writer reads them when it starts a file.
///
/// **It passes the sound on, sample for sample and with no latency**, so a
/// lane can go on to an Output Device after it and a Recorder can be put
/// anywhere without anybody having to think about what it does to the
/// sound. Several Recorders can hang off the same audio, each writing its
/// own file.
class Recorder final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "output.recorder";

    /// The formats, by the stable ids a session file holds.
    static constexpr std::string_view kFormatWav = "wav";
    static constexpr std::string_view kFormatAiff = "aiff";
    static constexpr std::string_view kFormatFlac = "flac";
    static constexpr std::string_view kFormatMp3 = "mp3";
    static constexpr std::string_view kFormatOpus = "opus";
    static constexpr std::string_view kFormatVorbis = "vorbis";
    /// M4A, through the system's FFmpeg where it has the encoder.
    static constexpr std::string_view kFormatAac = "aac";
    static constexpr std::string_view kFormatAlac = "alac";

    /// How each sample is stored, for the formats that store samples.
    static constexpr std::string_view kSampleInt16 = "int16";
    static constexpr std::string_view kSampleInt24 = "int24";
    static constexpr std::string_view kSampleFloat32 = "float32";

    /// The file's rate: the session's, or one of these, converted on the
    /// writer's thread.
    static constexpr std::string_view kRateSession = "session";
    static constexpr std::string_view kRate44100 = "44100";
    static constexpr std::string_view kRate48000 = "48000";
    static constexpr std::string_view kRate88200 = "88200";
    static constexpr std::string_view kRate96000 = "96000";

    /// **The default name: the session, then an ISO date and time**, so
    /// recordings sort by when they were made within each session.
    static constexpr std::string_view kDefaultFileName = "%session %date %time";

    enum Param : std::uint32_t {
        Format = 0,
        SampleFormat,
        /// Where files go. Empty means the default folder, which the engine
        /// works out: Reverblabr inside the user's music folder.
        Folder,
        /// The file name without its extension, with %session, %date,
        /// %time, %counter and %block in it (record::renderFileName).
        FileName,
        /// The file's sample rate (kRate…). **After the others**, so the
        /// positions a session's queued changes name stay where they were.
        SampleRate,
        /// File limits: a new file every SplitEvery SplitUnit, ...
        SplitEnabled,
        SplitEvery,
        SplitUnit,
        /// ... the recording ending after EndAfter EndUnit, ...
        EndEnabled,
        EndAfter,
        EndUnit,
        /// ... and what SilenceSeconds below SilenceThreshold does.
        SilenceEnabled,
        SilenceAction,
        SilenceSeconds,
        SilenceThreshold,
        /// Tags, text with the file-name tokens in it. Empty is not
        /// written.
        TagTitle,
        TagArtist,
        TagAlbum,
        TagGenre,
        TagYear,
        TagTrack,
        TagTrackCount,
        TagDisc,
        TagDiscCount,
        TagComment,
        /// For a lossy format: the rate aimed at, in kilobits a
        /// second, and whether it may vary with the sound.
        Bitrate,
        BitrateMode,
        /// Stereo, or mono: the two sides mixed into one channel.
        Channels,
    };

    static constexpr std::string_view kBitrateVariable = "vbr";
    static constexpr std::string_view kBitrateConstant = "cbr";
    static constexpr std::string_view kChannelsStereo = "stereo";
    static constexpr std::string_view kChannelsMono = "mono";

    /// The units of the limits, and what silence does, by the ids a session
    /// holds.
    static constexpr std::string_view kUnitMinutes = "minutes";
    static constexpr std::string_view kUnitHours = "hours";
    static constexpr std::string_view kUnitMegabytes = "megabytes";
    static constexpr std::string_view kSilenceRemove = "remove";
    static constexpr std::string_view kSilenceSplit = "split";
    static constexpr std::string_view kSilenceStop = "stop";

    Recorder();

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

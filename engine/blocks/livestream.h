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

/// Sends what arrives at it to a video service over RTMP, with a picture
/// made for it, and passes it on untouched.
///
/// **The audio thread's half, and nothing more**, as Broadcast is: it
/// copies every cycle into a tap and carries on. The picture, the encoders
/// and the network are `stream::LiveStream`'s, on a thread of its own
/// (ADR-0027). The settings are kept here because they belong to the block
/// in the session; **the stream key is not one of them**: it is kept in the
/// desktop's secret store, where a Broadcast block's password is.
class LiveStream final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "output.livestream";

    /// The quality presets, by the ids a session file holds
    /// (stream::LiveQualityPreset).
    static constexpr std::string_view kQualityGood = "good";
    static constexpr std::string_view kQualityHigher = "higher";
    static constexpr std::string_view kQualityBest = "best";

    /// The encoder: "auto" for the best one here, or one by name
    /// (stream::videoEncoderId()).
    static constexpr std::string_view kEncoderAuto = "auto";

    /// What moves with the sound.
    static constexpr std::string_view kVisualNone = "none";
    static constexpr std::string_view kVisualWaveform = "waveform";
    static constexpr std::string_view kVisualSpectrum = "spectrum";
    static constexpr std::string_view kVisualMeter = "meter";

    /// The title when it is left as it is: the session's name.
    static constexpr std::string_view kDefaultTitle = "%session";
    static constexpr std::string_view kDefaultBackground = "#16181d";

    enum Param : std::uint32_t {
        /// The service's address, rtmp:// or rtmps://.
        Url = 0,
        Quality,
        Encoder,
        /// The words on the picture: a title (with stream::renderTitle()'s
        /// %session), a subtitle, and what is playing, by its template.
        Title,
        Subtitle,
        TitleTemplate,
        /// Where what is playing comes from, as Broadcast's does
        /// (Broadcast::nowPlayingChoices()).
        NowPlaying,
        NowPlayingFile,
        NowPlayingPlayer,
        Visualisation,
        /// "#rrggbb", under everything.
        Background,
        /// Pictures: one covering the frame, and a logo at its top left.
        BackgroundImage,
        Logo,
    };

    LiveStream();

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

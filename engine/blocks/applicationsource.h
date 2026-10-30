// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audiobuffer.h"
#include "dsp/block.h"
#include "dsp/channelmap.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// One application's audio, taken as it plays.
///
/// **What the block does and what the engine does are different things.**
/// The engine finds the application's streams and links them to this block's
/// input ports — extra links, so the application goes on playing to its own
/// device and never knows ([ADR-0007]). By the time the audio is here it is
/// already summed by PipeWire, which sums everything arriving at a port; all
/// that is left for the block is to put whatever layout the application is
/// playing in onto the session's stereo bus.
///
/// **The match rule is what the session stores**, never an id: an
/// application is a binary name, or a Flatpak id, or a name it calls itself,
/// and a restarted application is the same application with new ids
/// ([ADR-0007]). The engine matches in that order.
///
/// **Its input ports follow the application.** A stream playing to a 5.1
/// sink arrives as six channels, and every one of them has to have a port to
/// arrive at, because a link only ever joins two ports of the same channel
/// and the mixing is done here with coefficients that can be written down
/// (see dsp::ChannelMapper). So the engine tells the block, through
/// PrepareSpec::inputLayout, what it is about to be given.
///
/// **It can take the microphone too.** A voice call has two sides, and only
/// one of them is the application playing; the other is what the
/// application is recording from. The engine follows the application's own
/// capture stream back to the source it is reading and links that source to
/// this block as well, so the block is given two groups of channels on one
/// set of ports and PrepareSpec::secondInputFrom says where the second
/// begins. Which microphone that is is not stored: it is whichever one the
/// application is using at this moment, so changing it in the application
/// changes it here.
///
/// **Split puts one side on each channel.** With it on, the microphone is
/// heard on the left and the application on the right, each folded to mono
/// **here** — not by linking a stream into both of our ports, which would
/// have PipeWire sum it and hand back 6 dB it never had.
class ApplicationSource final : public dsp::Block
{
public:
    /// The type id a session file holds.
    static constexpr std::string_view kTypeId = "source.application";

    /// Which of the block's published values is which (rt::BlockMeter). The
    /// engine writes the stream count; the interface turns it into "waiting
    /// for Firefox" or "capturing 2 streams", because the words are not the
    /// engine's business.
    static constexpr std::size_t kStreamCountValue = 0;

    /// Where each parameter sits in params().
    enum Param : std::uint32_t {
        Binary = 0, ///< application.process.binary, for example "firefox".
        FlatpakId, ///< The portal's application id, for a sandboxed one.
        ApplicationName, ///< application.name, the last thing to go on.
        /// Which of the application's streams to take, by where they are
        /// going: one of kOutputModes.
        OutputMode,
        /// The sinks named by the "chosen" mode, by node.name.
        Outputs,
        IncludeLowFrequency, ///< Whether a 5.1 stream's LFE is folded in.
        /// Also capture what the application is recording from, which is
        /// the other side of a call.
        IncludeInput,
        /// With IncludeInput: the microphone on the left, the application
        /// on the right, each folded to mono. Nothing without it, because
        /// there is only one thing to put on either side.
        SplitChannels,
        OpenUrlOnStart, ///< Open Url when the session starts.
        Url,
        /// For a Recorder downstream: whether a gap in what the
        /// application plays is written out as silence. The live engine
        /// goes on processing either way.
        FillGapsWithSilence,
    };

    /// **Which of an application's streams to capture.** A VoIP client plays
    /// the call to one device and a notification to another; a game plays to
    /// the headset and a browser to the speakers. The stable ids are what a
    /// session file holds.
    ///
    /// - `default` — only what it sends to the default output. What most
    ///   people mean by "the sound of this application".
    /// - `any` — everything it plays, wherever it goes.
    /// - `chosen` — only what it sends to the outputs named in Outputs.
    static constexpr std::string_view kOutputModeDefault = "default";
    static constexpr std::string_view kOutputModeAny = "any";
    static constexpr std::string_view kOutputModeChosen = "chosen";

    ApplicationSource();

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

    /// The name the microphone's ports carry, so that they can be told from
    /// the application's on a node that has both. The port is
    /// `<blockId>:in_mic_<channel>`.
    static constexpr std::string_view kMicrophoneRole = "mic";

    /// The layout the block was last prepared for, which is what its input
    /// ports carry: the application's channels, and then the microphone's
    /// when there is one.
    [[nodiscard]] const dsp::ChannelLayout &inputLayout() const noexcept
    {
        return m_layout;
    }

    /// What the microphone's ports carry, or empty when the block is not
    /// taking one.
    [[nodiscard]] const dsp::ChannelLayout &microphoneLayout() const noexcept
    {
        return m_microphoneLayout;
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    static void silence(const dsp::ProcessContext &context) noexcept RB_NONBLOCKING;
    /// Folds one group of channels to the stereo bus and then to one
    /// channel of the output.
    void foldToMono(const dsp::ChannelMapper &mapper,
                    const dsp::AudioBlock &input,
                    const dsp::ProcessContext &context,
                    std::uint32_t outputChannel) noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    /// Everything the input ports carry: the application's channels, then
    /// the microphone's.
    dsp::ChannelLayout m_layout;
    dsp::ChannelLayout m_applicationLayout;
    dsp::ChannelLayout m_microphoneLayout;
    /// Held by value once prepare() has made one; a mapper cannot be made
    /// without a layout to make it from.
    std::optional<dsp::ChannelMapper> m_mapper;
    std::optional<dsp::ChannelMapper> m_microphoneMapper;
    /// One stereo cycle's worth, for the group that is not being written
    /// straight to the output. Made in prepare(), like every other buffer.
    dsp::AudioBuffer m_scratch;
    bool m_prepared = false;
};

} // namespace rb::blocks

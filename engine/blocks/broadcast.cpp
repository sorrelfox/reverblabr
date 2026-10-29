// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/broadcast.h"

#include "rt/audiotap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

} // namespace

std::vector<ParamDescriptor> Broadcast::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    // **None of these moves a port or a link**, so they are all automatable
    // in the engine's sense: changing one while the session runs costs no
    // replan. The stream takes each change as it comes, connecting again
    // when the server has to be told it in its first request.
    const auto text = [&descriptors](const char *id, std::string_view fallback) {
        ParamDescriptor descriptor;
        descriptor.id = id;
        descriptor.type = ParamType::String;
        descriptor.defaultValue = ParamValue::ofString(std::string{fallback});
        descriptors.push_back(std::move(descriptor));
    };
    const auto choice = [&descriptors](const char *id, std::vector<std::string> choices, std::string_view fallback) {
        ParamDescriptor descriptor;
        descriptor.id = id;
        descriptor.type = ParamType::Enum;
        descriptor.choices = std::move(choices);
        descriptor.defaultValue = ParamValue::ofEnum(std::string{fallback});
        descriptors.push_back(std::move(descriptor));
    };
    const auto number =
        [&descriptors](const char *id, std::int64_t lowest, std::int64_t highest, std::int64_t fallback) {
            ParamDescriptor descriptor;
            descriptor.id = id;
            descriptor.type = ParamType::Int;
            descriptor.minimum = ParamValue::ofInt(lowest);
            descriptor.maximum = ParamValue::ofInt(highest);
            descriptor.defaultValue = ParamValue::ofInt(fallback);
            descriptors.push_back(std::move(descriptor));
        };

    choice("server", {std::string{kServerIcecast}, std::string{kServerShoutcast}}, kServerIcecast);
    // **A server on this computer out of the box**, at Icecast's own port:
    // what somebody trying it out has, and nothing is sent anywhere until
    // the session runs with a password set.
    text("host", "localhost");
    number("port", 1, 65535, 8000);
    text("mount", "/stream");
    text("user", "source");
    choice("format",
           {std::string{kFormatMp3}, std::string{kFormatOpus}, std::string{kFormatVorbis}, std::string{kFormatAac}},
           kFormatMp3);
    // 128 kbit/s MP3: what most stations send, and what a listener on a
    // phone can keep up with.
    number("bitrate", 32, 320, 128);
    choice("channels", {std::string{kChannelsStereo}, std::string{kChannelsMono}}, kChannelsStereo);

    text("stationName", {});
    text("genre", {});
    text("url", {});
    text("description", {});
    ParamDescriptor listed;
    listed.id = "listed";
    listed.type = ParamType::Bool;
    // **Not listed unless asked**: a public directory is somebody else's
    // web page.
    listed.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(listed));

    text("titleTemplate", kDefaultTitleTemplate);
    choice("nowPlaying",
           {std::string{kNowPlayingNone}, std::string{kNowPlayingFile}, std::string{kNowPlayingPlayer}},
           kNowPlayingNone);
    text("nowPlayingFile", {});
    // **The player the session captures, unless one is named**: what
    // somebody streaming the music they play means, and it follows them
    // from one player to another.
    text("nowPlayingPlayer", {});
    return descriptors;
}

Broadcast::Broadcast()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Outputs;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Icon;
    m_info.latencySamples = 0;
    m_info.streamsAudio = true;
}

std::unique_ptr<Block> Broadcast::make()
{
    return std::make_unique<Broadcast>();
}

std::expected<void, BlockError> Broadcast::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_tap = spec.recording;
    m_sampleRate = static_cast<std::uint32_t>(std::lround(spec.sampleRate));
    return {};
}

void Broadcast::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    std::array<const float *, kChannels> streamed{};
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        // A channel with no storage is silence, which the tap writes as
        // zeros when it is handed a null pointer.
        streamed[channel] = in.size() >= context.frames ? in.data() : nullptr;
        if (channel >= channels) {
            continue;
        }
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        // **Sample for sample.** Streaming something must not change it.
        if (streamed[channel] != nullptr) {
            std::copy_n(streamed[channel], context.frames, out.data());
        } else {
            std::fill_n(out.data(), context.frames, 0.0f);
        }
    }

    if (m_tap != nullptr) {
        // **Every cycle, pauses and all.** A stream is a radio station:
        // its listeners hear time pass, and a source that has stopped
        // playing is silence on the air rather than a gap the stream
        // closes up, as a recording does.
        m_tap->announceRate(m_sampleRate);
        (void)m_tap->write(streamed, context.frames);
    }
}

void Broadcast::reset() noexcept RB_NONBLOCKING
{
    // Nothing is remembered from one cycle to the next: what has been sent
    // is the stream's, and the next cycle is simply the next frames.
}

void Broadcast::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Broadcast::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Broadcast::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Broadcast::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Broadcast::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

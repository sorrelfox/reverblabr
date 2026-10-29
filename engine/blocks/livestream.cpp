// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/livestream.h"

#include "blocks/broadcast.h"
#include "blocks/streamtap.h"

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

std::vector<ParamDescriptor> LiveStream::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    // **None of these moves a port or a link**: changing one while the
    // session runs costs no replan. The stream takes each as it comes,
    // connecting again for a new address, quality or encoder, and only
    // redrawing for the rest.
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

    // **No address out of the box**: nothing goes anywhere until somebody
    // names a service.
    text("url", {});
    choice(
        "quality", {std::string{kQualityGood}, std::string{kQualityHigher}, std::string{kQualityBest}}, kQualityHigher);
    choice("encoder", {std::string{kEncoderAuto}, "vaapi", "nvenc", "x264", "openh264"}, kEncoderAuto);
    text("title", kDefaultTitle);
    text("subtitle", {});
    text("titleTemplate", Broadcast::kDefaultTitleTemplate);
    // **The same sources as Broadcast's**, by the same ids, so the engine
    // follows a media player for either block alike.
    choice("nowPlaying", Broadcast::nowPlayingChoices(), Broadcast::kNowPlayingNone);
    text("nowPlayingFile", {});
    text("nowPlayingPlayer", {});
    choice("visualisation",
           {std::string{kVisualNone},
            std::string{kVisualWaveform},
            std::string{kVisualSpectrum},
            std::string{kVisualMeter}},
           kVisualWaveform);
    text("background", kDefaultBackground);
    text("backgroundImage", {});
    text("logo", {});
    return descriptors;
}

LiveStream::LiveStream()
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

std::unique_ptr<Block> LiveStream::make()
{
    return std::make_unique<LiveStream>();
}

std::expected<void, BlockError> LiveStream::prepare(const PrepareSpec &spec)
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

void LiveStream::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    passOnAndTap(context, m_tap, m_sampleRate);
}

void LiveStream::reset() noexcept RB_NONBLOCKING
{
    // Nothing is remembered from one cycle to the next.
}

void LiveStream::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> LiveStream::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue LiveStream::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap LiveStream::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> LiveStream::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

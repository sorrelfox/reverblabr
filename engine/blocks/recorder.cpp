// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/recorder.h"

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

std::vector<ParamDescriptor> Recorder::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    // **None of these moves a port or a link**, so they are all automatable
    // in the engine's sense: changing one while the session runs costs no
    // replan. Each takes effect at the next file, because a file cannot
    // change its format half-way through.
    ParamDescriptor format;
    format.id = "format";
    format.type = ParamType::Enum;
    format.choices = {std::string{kFormatWav},
                      std::string{kFormatAiff},
                      std::string{kFormatFlac},
                      std::string{kFormatMp3},
                      std::string{kFormatOpus},
                      std::string{kFormatVorbis},
                      std::string{kFormatAac},
                      std::string{kFormatAlac}};
    // **High-quality MP3 out of the box**: what people send each other
    // and every player opens, at a rate nobody hears through.
    format.defaultValue = ParamValue::ofEnum(std::string{kFormatMp3});
    descriptors.push_back(std::move(format));

    ParamDescriptor sampleFormat;
    sampleFormat.id = "sampleFormat";
    sampleFormat.type = ParamType::Enum;
    sampleFormat.choices = {std::string{kSampleInt16}, std::string{kSampleInt24}, std::string{kSampleFloat32}};
    sampleFormat.defaultValue = ParamValue::ofEnum(std::string{kSampleInt24});
    descriptors.push_back(std::move(sampleFormat));

    ParamDescriptor folder;
    folder.id = "folder";
    folder.type = ParamType::String;
    folder.defaultValue = ParamValue::ofString(std::string{});
    descriptors.push_back(std::move(folder));

    ParamDescriptor fileName;
    fileName.id = "fileName";
    fileName.type = ParamType::String;
    fileName.defaultValue = ParamValue::ofString(std::string{kDefaultFileName});
    descriptors.push_back(std::move(fileName));

    ParamDescriptor rate;
    rate.id = "sampleRate";
    rate.type = ParamType::Enum;
    rate.choices = {std::string{kRateSession},
                    std::string{kRate44100},
                    std::string{kRate48000},
                    std::string{kRate88200},
                    std::string{kRate96000}};
    rate.defaultValue = ParamValue::ofEnum(std::string{kRateSession});
    descriptors.push_back(std::move(rate));

    // File limits. Each has a switch of its own, so turning one off
    // keeps the number that was there for when it is turned on again.
    const auto toggle = [&descriptors](const char *id) {
        ParamDescriptor descriptor;
        descriptor.id = id;
        descriptor.type = ParamType::Bool;
        descriptor.defaultValue = ParamValue::ofBool(false);
        descriptors.push_back(std::move(descriptor));
    };
    const auto count = [&descriptors](const char *id, std::int64_t fallback) {
        ParamDescriptor descriptor;
        descriptor.id = id;
        descriptor.type = ParamType::Int;
        descriptor.minimum = ParamValue::ofInt(1);
        descriptor.maximum = ParamValue::ofInt(100000);
        descriptor.defaultValue = ParamValue::ofInt(fallback);
        descriptors.push_back(std::move(descriptor));
    };
    const auto choice = [&descriptors](const char *id, std::vector<std::string> choices, std::string fallback) {
        ParamDescriptor descriptor;
        descriptor.id = id;
        descriptor.type = ParamType::Enum;
        descriptor.choices = std::move(choices);
        descriptor.defaultValue = ParamValue::ofEnum(std::move(fallback));
        descriptors.push_back(std::move(descriptor));
    };
    toggle("splitEnabled");
    count("splitEvery", 30);
    choice("splitUnit",
           {std::string{kUnitMinutes}, std::string{kUnitHours}, std::string{kUnitMegabytes}},
           std::string{kUnitMinutes});
    toggle("endEnabled");
    count("endAfter", 1);
    choice("endUnit", {std::string{kUnitMinutes}, std::string{kUnitHours}}, std::string{kUnitHours});
    toggle("silenceEnabled");
    choice("silenceAction",
           {std::string{kSilenceRemove}, std::string{kSilenceSplit}, std::string{kSilenceStop}},
           std::string{kSilenceRemove});

    ParamDescriptor silenceSeconds;
    silenceSeconds.id = "silenceSeconds";
    silenceSeconds.type = ParamType::Float;
    silenceSeconds.minimum = ParamValue::ofFloat(0.1f);
    silenceSeconds.maximum = ParamValue::ofFloat(3600.0f);
    silenceSeconds.defaultValue = ParamValue::ofFloat(2.0f);
    descriptors.push_back(std::move(silenceSeconds));

    // From digital silence (−96 dB, nothing but zeros) to the hiss of a
    // quiet room (−20 dB would be a room with a fan in it).
    ParamDescriptor threshold;
    threshold.id = "silenceThreshold";
    threshold.type = ParamType::Float;
    threshold.minimum = ParamValue::ofFloat(-96.0f);
    threshold.maximum = ParamValue::ofFloat(-20.0f);
    threshold.defaultValue = ParamValue::ofFloat(-60.0f);
    threshold.unit = ParamUnit::Decibels;
    descriptors.push_back(std::move(threshold));

    // Tags: text, tokens allowed, nothing written while empty. **The
    // numbers are text too**, so "%counter" can number the files a
    // recording splits into.
    for (const char *id : {"tagTitle",
                           "tagArtist",
                           "tagAlbum",
                           "tagGenre",
                           "tagYear",
                           "tagTrack",
                           "tagTrackCount",
                           "tagDisc",
                           "tagDiscCount",
                           "tagComment"}) {
        ParamDescriptor tag;
        tag.id = id;
        tag.type = ParamType::String;
        tag.defaultValue = ParamValue::ofString(std::string{});
        descriptors.push_back(std::move(tag));
    }

    // The format options.
    ParamDescriptor bitrate;
    bitrate.id = "bitrate";
    bitrate.type = ParamType::Int;
    bitrate.minimum = ParamValue::ofInt(32);
    bitrate.maximum = ParamValue::ofInt(320);
    bitrate.defaultValue = ParamValue::ofInt(256);
    descriptors.push_back(std::move(bitrate));
    choice(
        "bitrateMode", {std::string{kBitrateVariable}, std::string{kBitrateConstant}}, std::string{kBitrateVariable});
    choice("channels", {std::string{kChannelsStereo}, std::string{kChannelsMono}}, std::string{kChannelsStereo});
    return descriptors;
}

Recorder::Recorder()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Outputs;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Icon;
    m_info.latencySamples = 0;
    m_info.recordsAudio = true;
}

std::unique_ptr<Block> Recorder::make()
{
    return std::make_unique<Recorder>();
}

std::expected<void, BlockError> Recorder::prepare(const PrepareSpec &spec)
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

void Recorder::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    std::array<const float *, kChannels> recorded{};
    for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        // A channel with no storage is silence, which the tap writes as
        // zeros when it is handed a null pointer.
        recorded[channel] = in.size() >= context.frames ? in.data() : nullptr;
        if (channel >= channels) {
            continue;
        }
        const std::span<float> out = context.output.channel(channel);
        if (out.empty()) {
            continue;
        }
        // **Sample for sample.** Recording something must not change it.
        if (recorded[channel] != nullptr) {
            std::copy_n(recorded[channel], context.frames, out.data());
        } else {
            std::fill_n(out.data(), context.frames, 0.0f);
        }
    }

    if (m_tap != nullptr) {
        // **A pause is left out, not written as silence**, when every
        // source feeding this is an application that has stopped playing
        // and none of them asked for its silence to be kept.
        const std::uint32_t gapLanes = m_tap->gapLanes();
        if (gapLanes != 0 && (context.sourcePresent & gapLanes) == 0) {
            return;
        }
        // Before the frames, so the writer knows what rate they are at when
        // it opens the file for them.
        m_tap->announceRate(m_sampleRate);
        (void)m_tap->write(recorded, context.frames);
    }
}

void Recorder::reset() noexcept RB_NONBLOCKING
{
    // Nothing is remembered from one cycle to the next: what has been
    // written is the writer's, and the next cycle is simply the next frames.
}

void Recorder::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Recorder::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Recorder::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Recorder::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Recorder::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

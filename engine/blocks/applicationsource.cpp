// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/applicationsource.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

/// The widest layout a source can be given ports for, and the names those
/// ports take. A stream with more channels than this is not something M1
/// meets; the engine gives the block what it can carry.
[[nodiscard]] PortSpec inputPorts()
{
    return PortSpec{1, kMaxMappedChannels, {}, 0, {}};
}

[[nodiscard]] ParamDescriptor boolParam(std::string_view id, bool byDefault)
{
    ParamDescriptor descriptor;
    descriptor.id = std::string{id};
    descriptor.type = ParamType::Bool;
    descriptor.defaultValue = ParamValue::ofBool(byDefault);
    return descriptor;
}

/// What a layout folds down to when it is one side of a split: the stereo
/// downmix, and then the two halves at the same weight the channel policy
/// gives a stereo pair going to one channel.
constexpr float kMonoWeight = 0.5f;

[[nodiscard]] ParamDescriptor textParam(std::string_view id)
{
    ParamDescriptor descriptor;
    descriptor.id = std::string{id};
    descriptor.type = ParamType::String;
    descriptor.defaultValue = ParamValue::ofString(std::string{});
    // Every text this block holds names something outside it — an
    // application to find, an address to open — and the plan is what goes
    // looking. None of them is a number the audio thread could use.
    descriptor.flags.automatable = false;
    return descriptor;
}

/// Marks a parameter the session plan reads. **Not automatable**: what has
/// to change for it is which streams are captured and which ports they
/// arrive on, so the engine plans the session again rather than sending the
/// block a value nothing would look at.
[[nodiscard]] ParamDescriptor structural(ParamDescriptor descriptor)
{
    descriptor.flags.automatable = false;
    return descriptor;
}

} // namespace

std::vector<ParamDescriptor> ApplicationSource::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    // **Three fields, not one rule.** A session written today has to go on
    // finding the application on a machine where it was installed another
    // way — from a distribution package one day and from Flatpak the next —
    // so each way of naming it is stored on its own and the engine tries
    // them in turn.
    descriptors.push_back(textParam("binary"));
    descriptors.push_back(textParam("flatpakId"));
    descriptors.push_back(textParam("name"));

    ParamDescriptor mode;
    mode.id = "outputMode";
    mode.type = ParamType::Enum;
    mode.choices = {std::string{ApplicationSource::kOutputModeDefault},
                    std::string{ApplicationSource::kOutputModeAny},
                    std::string{ApplicationSource::kOutputModeChosen}};
    // The default output is what most people mean by "the sound of this
    // application": what it sends anywhere else, it sends there on purpose.
    mode.defaultValue = ParamValue::ofEnum(std::string{ApplicationSource::kOutputModeDefault});
    descriptors.push_back(structural(std::move(mode)));

    ParamDescriptor outputs;
    outputs.id = "outputs";
    outputs.type = ParamType::StringList;
    outputs.defaultValue = ParamValue::ofStringList({});
    descriptors.push_back(structural(std::move(outputs)));

    // The downmix is built when the block is prepared, which happens as
    // part of planning the session and nowhere else.
    descriptors.push_back(structural(boolParam("includeLowFrequency", false)));

    // **The other side of the call.** Off by default: capturing a
    // microphone is something to ask for, never something that happens
    // because a block was dropped on the canvas. Structural, because the
    // microphone has to be found and given ports of its own before there
    // is anything to mix.
    descriptors.push_back(structural(boolParam("includeInput", false)));
    // **Not structural**, unlike the one above it: this only chooses
    // between two ways of mixing channels that have already arrived, and
    // the block reads it at the top of every cycle.
    descriptors.push_back(boolParam("splitChannels", false));

    descriptors.push_back(structural(boolParam("openUrlOnStart", false)));
    descriptors.push_back(textParam("url"));

    // **On by default**, because a recording that leaves a pause out is a
    // recording that no longer lines up with anything beside it. Nothing
    // reads this yet; the Recorder does (M2).
    descriptors.push_back(boolParam("fillGapsWithSilence", true));
    return descriptors;
}

ApplicationSource::ApplicationSource()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
    , m_layout(stereoLayout())
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Sources;
    // The audio system fills the inputs; a wire into them is refused.
    m_info.inputsFromAudioSystem = true;
    m_info.inputs = inputPorts();
    m_info.outputs = PortSpec::fixed(2, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Meter;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> ApplicationSource::make()
{
    return std::make_unique<ApplicationSource>();
}

std::expected<void, BlockError> ApplicationSource::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    // What the engine says it is about to send, or a stereo pair when it has
    // not said: an application that is not playing yet still needs ports to
    // be linked to when it starts.
    ChannelLayout layout = spec.inputLayout;
    if (layout.empty()) {
        layout.assign(spec.inputChannels == 0 ? 2 : spec.inputChannels, ChannelPosition::Unknown);
        if (layout.size() == 1) {
            layout[0] = ChannelPosition::Mono;
        } else if (layout.size() == 2) {
            layout = stereoLayout();
        }
    }

    // **Two groups on one set of ports.** The application's channels come
    // first and the microphone's after them; the engine says where the
    // second begins, because both have channels of the same name and
    // nothing about a port says which group it belongs to.
    const std::size_t split = spec.secondInputFrom == 0 || spec.secondInputFrom > layout.size()
        ? layout.size()
        : static_cast<std::size_t>(spec.secondInputFrom);
    const ChannelLayout applicationLayout(layout.begin(), layout.begin() + static_cast<std::ptrdiff_t>(split));
    const ChannelLayout microphoneLayout(layout.begin() + static_cast<std::ptrdiff_t>(split), layout.end());

    DownmixOptions options;
    options.includeLowFrequency = m_store.number(IncludeLowFrequency) != 0.0f;
    const std::expected<ChannelMapper, ChannelMapError> mapper = ChannelMapper::toStereo(applicationLayout, options);
    if (!mapper) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    std::optional<ChannelMapper> microphone;
    if (!microphoneLayout.empty()) {
        const std::expected<ChannelMapper, ChannelMapError> made = ChannelMapper::toStereo(microphoneLayout, options);
        if (!made) {
            return std::unexpected(BlockError::UnsupportedSpec);
        }
        microphone = *made;
    }

    // **The block says how many input ports it has once it has been told
    // what it will be given.** The compiler works a flexible port count out
    // from the wires that arrive at it, and nothing wires a source: its
    // input is the application, which the engine knows about and the graph
    // does not.
    std::vector<std::string> labels;
    labels.reserve(layout.size());
    for (std::size_t channel = 0; channel < layout.size(); ++channel) {
        // The microphone's front left and the application's front left are
        // both "FL", so the name is what keeps them apart.
        const std::string role = channel < split ? std::string{} : std::string{kMicrophoneRole} + "_";
        labels.emplace_back("in_" + role + std::string{channelPositionName(layout[channel])});
    }
    m_info.inputs = PortSpec::fixed(static_cast<std::uint32_t>(layout.size()), std::move(labels));

    m_scratch.resize(2, spec.maxBlockFrames);
    m_layout = std::move(layout);
    m_applicationLayout = applicationLayout;
    m_microphoneLayout = microphoneLayout;
    m_mapper = *mapper;
    m_microphoneMapper = microphone;
    m_prepared = true;
    return {};
}

void ApplicationSource::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    // PipeWire has already summed every stream that arrives at a port, which
    // is right: they are different sounds from one application. What is left
    // is the layout, and that is the mapper's whole job — the one place
    // channels are mixed, with coefficients that were written down first.
    if (!m_mapper) {
        // Nothing should run a block nobody prepared, but a buffer that is
        // not written carries whatever the last user of the memory left in
        // it, and that is a noise nobody can explain.
        silence(context);
        return;
    }

    const auto applicationChannels = static_cast<std::uint32_t>(m_applicationLayout.size());
    const AudioBlock application = context.input.channels(0, applicationChannels);
    const AudioBlock microphone =
        context.input.channels(applicationChannels, static_cast<std::uint32_t>(m_microphoneLayout.size()));

    // **The split belongs to the option, not to the microphone.** With it
    // on and nothing recording, the left side says nothing — which is true
    // — rather than quietly becoming the application again.
    const bool includeInput = m_store.number(IncludeInput) != 0.0f;
    if (includeInput && m_store.number(SplitChannels) != 0.0f) {
        // **One side of the call on each channel**, each folded to mono
        // here. Linking a stream into both of our ports would have PipeWire
        // sum it and give back 6 dB it never had.
        silence(context);
        if (m_microphoneMapper) {
            foldToMono(*m_microphoneMapper, microphone, context, 0);
        }
        foldToMono(*m_mapper, application, context, 1);
        return;
    }

    m_mapper->process(application, context.output, context.frames);
    if (!m_microphoneMapper) {
        return;
    }
    // Both sides on the stereo bus, which is what "include the microphone"
    // means when it is not split: one conversation, not two tracks.
    const AudioBlock scratch = m_scratch.block(context.frames);
    m_microphoneMapper->process(microphone, scratch, context.frames);
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        const std::span<float> from = scratch.channel(channel);
        for (std::uint32_t frame = 0; frame < context.frames && frame < out.size() && frame < from.size(); ++frame) {
            out[frame] += from[frame];
        }
    }
}

void ApplicationSource::silence(const ProcessContext &context) noexcept RB_NONBLOCKING
{
    for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
        const std::span<float> out = context.output.channel(channel);
        std::fill_n(out.data(), std::min<std::size_t>(out.size(), context.frames), 0.0f);
    }
}

void ApplicationSource::foldToMono(const ChannelMapper &mapper,
                                   const AudioBlock &input,
                                   const ProcessContext &context,
                                   std::uint32_t outputChannel) noexcept RB_NONBLOCKING
{
    const std::span<float> out = context.output.channel(outputChannel);
    if (out.empty()) {
        return;
    }
    // To the stereo bus first, so that a 5.1 side folds by the same
    // coefficients as every other downmix, and then the pair to one channel
    // at the weight the policy gives a stereo pair going to one.
    const AudioBlock scratch = m_scratch.block(context.frames);
    mapper.process(input, scratch, context.frames);
    const std::span<float> left = scratch.channel(0);
    const std::span<float> right = scratch.channel(1);
    for (std::uint32_t frame = 0; frame < context.frames && frame < out.size(); ++frame) {
        const float a = frame < left.size() ? left[frame] : 0.0f;
        const float b = frame < right.size() ? right[frame] : 0.0f;
        out[frame] = kMonoWeight * (a + b);
    }
}

void ApplicationSource::reset() noexcept RB_NONBLOCKING
{
    // Nothing is remembered between cycles: the mapper is a matrix.
}

void ApplicationSource::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // The channel split is the one number read while the block runs: it
    // chooses between two ways of mixing what the ports already carry.
    // Everything the session plan reads is structural and never arrives
    // here, because the engine plans the session again for it instead.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> ApplicationSource::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue ApplicationSource::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap ApplicationSource::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> ApplicationSource::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/volume.h"

#include <algorithm>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

/// What each overdrive setting makes the top of the slider.
} // namespace

std::vector<ParamDescriptor> Volume::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor gain;
    gain.id = "gain";
    gain.type = ParamType::Float;
    gain.minimum = ParamValue::ofFloat(0.0f);
    // The range is the widest any overdrive setting allows; which part of it
    // the slider offers is the overdrive's business.
    gain.maximum = ParamValue::ofFloat(4.0f);
    gain.defaultValue = ParamValue::ofFloat(1.0f);
    gain.smoothingMs = Volume::kGainSmoothingMs;
    descriptors.push_back(std::move(gain));

    ParamDescriptor overdrive;
    overdrive.id = "overdrive";
    overdrive.type = ParamType::Enum;
    overdrive.choices = {std::string{Volume::kOverdriveOff},
                         std::string{Volume::kOverdrive2x},
                         std::string{Volume::kOverdrive3x},
                         std::string{Volume::kOverdrive4x}};
    overdrive.defaultValue = ParamValue::ofEnum(std::string{Volume::kOverdriveOff});
    descriptors.push_back(std::move(overdrive));

    ParamDescriptor muted;
    muted.id = "muted";
    muted.type = ParamType::Bool;
    // **The ramp is the block's, not the descriptor's.** Smoothing on the
    // descriptor is for a value that has a range to glide along; a mute is
    // on or off, and what is smoothed is what the block does about it.
    muted.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(muted));
    return descriptors;
}

Volume::Volume()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Volume::make()
{
    return std::make_unique<Volume>();
}

float Volume::maximumGain() const noexcept RB_NONBLOCKING
{
    // **The choice's number, not its id.** Asking the store for the value
    // would build a ParamValue holding a string, and a string on the audio
    // thread is an allocation waiting for a longer name — clang's
    // -Wfunction-effects says so, and it is right. The choices are listed
    // off, 2x, 3x, 4x, so the index and the multiplier differ by one.
    return m_store.number(Overdrive) + 1.0f;
}

void Volume::clampToOverdrive()
{
    const float highest = maximumGain();
    if (m_store.number(Gain) > highest) {
        // What is stored is what is heard: leaving 400 % in the file under a
        // slider that stops at 100 % would be a number nobody can see.
        (void)m_store.set(Gain, ParamValue::ofFloat(highest));
    }
}

std::expected<void, BlockError> Volume::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }

    m_gain.prepare(spec.sampleRate, kGainSmoothingMs);
    m_gain.reset(std::min(m_store.number(Gain), maximumGain()));
    m_mute.prepare(spec.sampleRate, kMuteSmoothingMs);
    m_mute.reset(m_store.number(Muted) != 0.0f ? 0.0f : 1.0f);
    m_limiter.prepare(spec.sampleRate, kChannels);
    // **The limiter is always in the path**, so this is a constant and the
    // graph can line the paths up around it.
    m_info.latencySamples = m_limiter.latencySamples();
    m_prepared = true;
    return {};
}

void Volume::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    m_gain.setTarget(std::min(m_store.number(Gain), maximumGain()));
    m_mute.setTarget(m_store.number(Muted) != 0.0f ? 0.0f : 1.0f);

    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // One gain for both sides, so nothing pulls the image over while it
        // moves.
        const float gain = m_gain.next() * m_mute.next();
        for (std::uint32_t channel = 0; channel < channels; ++channel) {
            const std::span<float> in = context.input.channel(channel);
            const std::span<float> out = context.output.channel(channel);
            if (frame < out.size()) {
                out[frame] = frame < in.size() ? in[frame] * gain : 0.0f;
            }
        }
    }

    // **After the gain, not before it.** What the limiter has to keep below
    // the ceiling is what this block is about to send on, overdrive and all.
    m_limiter.process(context.output, context.frames);
}

void Volume::reset() noexcept RB_NONBLOCKING
{
    m_gain.reset(std::min(m_store.number(Gain), maximumGain()));
    m_mute.reset(m_store.number(Muted) != 0.0f ? 0.0f : 1.0f);
    m_limiter.reset();
}

void Volume::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // The gain and the mute both reach the audio thread this way; the
    // smoothers read them at the top of every cycle, so a change that
    // arrives mid-drag is heard as a glide and not a step.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> Volume::setParam(std::string_view id, const ParamValue &value)
{
    const std::expected<void, BlockError> set = m_store.set(id, value);
    if (set && id == "overdrive") {
        clampToOverdrive();
    }
    return set;
}

ParamValue Volume::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Volume::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Volume::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded) {
        clampToOverdrive();
        if (m_prepared) {
            m_gain.reset(std::min(m_store.number(Gain), maximumGain()));
            m_mute.reset(m_store.number(Muted) != 0.0f ? 0.0f : 1.0f);
        }
    }
    return loaded;
}

} // namespace rb::blocks

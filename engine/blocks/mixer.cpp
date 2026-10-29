// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/mixer.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::array<std::string_view, Mixer::kScenes> kSceneNames{"A", "B", "C", "D", "E"};

[[nodiscard]] ParamDescriptor number(std::string id, float lowest, float highest, float byDefault, ParamUnit unit)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(lowest);
    descriptor.maximum = ParamValue::ofFloat(highest);
    descriptor.defaultValue = ParamValue::ofFloat(byDefault);
    descriptor.unit = unit;
    return descriptor;
}

[[nodiscard]] ParamDescriptor toggle(std::string id)
{
    ParamDescriptor descriptor;
    descriptor.id = std::move(id);
    descriptor.type = ParamType::Bool;
    descriptor.defaultValue = ParamValue::ofBool(false);
    return descriptor;
}

[[nodiscard]] ParamDescriptor hidden(ParamDescriptor descriptor)
{
    descriptor.flags.hidden = true;
    return descriptor;
}

} // namespace

std::vector<ParamDescriptor> Mixer::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    for (std::uint32_t strip = 1; strip <= kStrips; ++strip) {
        const std::string n = std::to_string(strip);
        // **An amplitude shown as a percentage**, as the Volume block's is:
        // 2.0 is 200 %.
        descriptors.push_back(number("volume" + n, 0.0f, 2.0f, 1.0f, ParamUnit::Percent));
        descriptors.push_back(toggle("mute" + n));
        descriptors.push_back(toggle("solo" + n));
        descriptors.push_back(number("balance" + n, -1.0f, 1.0f, 0.0f, ParamUnit::None));
        // How long the next change of this strip's volume takes. Hidden: it
        // is how a fade is sent, not a setting anybody sets.
        descriptors.push_back(hidden(number("ramp" + n, 0.0f, 10000.0f, kDefaultRampMs, ParamUnit::Milliseconds)));
    }
    descriptors.push_back(number("master", 0.0f, 2.0f, 1.0f, ParamUnit::Percent));
    for (const std::string_view scene : kSceneNames) {
        for (std::uint32_t strip = 1; strip <= kStrips; ++strip) {
            descriptors.push_back(hidden(
                number("scene" + std::string{scene} + std::to_string(strip), 0.0f, 2.0f, 1.0f, ParamUnit::Percent)));
        }
        descriptors.push_back(hidden(toggle("scene" + std::string{scene} + "Saved")));
    }
    return descriptors;
}

Mixer::Mixer()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    std::vector<std::string> lanes;
    std::vector<std::string> ports;
    for (std::uint32_t strip = 1; strip <= kStrips; ++strip) {
        const std::string lane = "input" + std::to_string(strip);
        lanes.push_back(lane);
        ports.push_back(lane + "_FL");
        ports.push_back(lane + "_FR");
    }
    m_info.inputs = PortSpec::lanes(kLaneWidth, std::move(lanes), std::move(ports));
    m_info.outputs = PortSpec::fixed(kLaneWidth, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> Mixer::make()
{
    return std::make_unique<Mixer>();
}

std::uint32_t Mixer::samplesOf(float ms) const noexcept RB_NONBLOCKING
{
    return static_cast<std::uint32_t>(std::max(0.0, std::round(static_cast<double>(ms) * 1.0e-3 * m_sampleRate)));
}

std::expected<void, BlockError> Mixer::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    for (std::uint32_t strip = 0; strip < kStrips; ++strip) {
        m_gates[strip].prepare(spec.sampleRate, kSmoothingMs);
        m_balances[strip].prepare(spec.sampleRate, kSmoothingMs);
    }
    m_master.prepare(spec.sampleRate, kSmoothingMs);
    reset();
    return {};
}

void Mixer::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    // **Solo is heard over everything else**: while any strip is soloed,
    // only the soloed ones are, and a muted one is silent whatever else it
    // is.
    bool anySolo = false;
    for (std::uint32_t strip = 0; strip < kStrips; ++strip) {
        anySolo = anySolo || m_store.number(indexOf(strip, Solo)) != 0.0f;
    }
    for (std::uint32_t strip = 0; strip < kStrips; ++strip) {
        const bool muted = m_store.number(indexOf(strip, Mute)) != 0.0f;
        const bool soloed = m_store.number(indexOf(strip, Solo)) != 0.0f;
        m_gates[strip].setTarget(muted || (anySolo && !soloed) ? 0.0f : 1.0f);
        m_balances[strip].setTarget(balanceGains(m_store.number(indexOf(strip, Balance))));
    }
    m_master.setTarget(m_store.number(kMaster));

    const std::uint32_t inputs = context.input.channelCount();
    const std::uint32_t outputs = std::min(context.output.channelCount(), kLaneWidth);
    std::array<std::span<float>, kStrips * kLaneWidth> in{};
    for (std::uint32_t port = 0; port < in.size() && port < inputs; ++port) {
        in[port] = context.input.channel(port);
    }
    const std::span<float> left = outputs > 0 ? context.output.channel(0) : std::span<float>{};
    const std::span<float> right = outputs > 1 ? context.output.channel(1) : std::span<float>{};

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        float sumLeft = 0.0f;
        float sumRight = 0.0f;
        for (std::uint32_t strip = 0; strip < kStrips; ++strip) {
            const float level = m_volumes[strip].next() * m_gates[strip].next();
            const StereoGains pan = m_balances[strip].next();
            const std::span<float> l = in[strip * kLaneWidth];
            const std::span<float> r = in[strip * kLaneWidth + 1];
            if (frame < l.size()) {
                sumLeft += l[frame] * level * pan.left;
            }
            if (frame < r.size()) {
                sumRight += r[frame] * level * pan.right;
            }
        }
        const float master = m_master.next();
        if (frame < left.size()) {
            left[frame] = sumLeft * master;
        }
        if (frame < right.size()) {
            right[frame] = sumRight * master;
        }
    }
}

void Mixer::reset() noexcept RB_NONBLOCKING
{
    bool anySolo = false;
    for (std::uint32_t strip = 0; strip < kStrips; ++strip) {
        anySolo = anySolo || m_store.number(indexOf(strip, Solo)) != 0.0f;
    }
    for (std::uint32_t strip = 0; strip < kStrips; ++strip) {
        m_volumes[strip].jump(m_store.number(indexOf(strip, Volume)));
        const bool muted = m_store.number(indexOf(strip, Mute)) != 0.0f;
        const bool soloed = m_store.number(indexOf(strip, Solo)) != 0.0f;
        m_gates[strip].reset(muted || (anySolo && !soloed) ? 0.0f : 1.0f);
        m_balances[strip].reset(balanceGains(m_store.number(indexOf(strip, Balance))));
    }
    m_master.reset(m_store.number(kMaster));
}

void Mixer::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
    if (index >= kMaster) {
        return;
    }
    if (index % kPerStrip == Volume) {
        // **The ramp this strip was last told**, which for a fade arrived a
        // moment ago in the same edit and is put back just after this.
        const std::uint32_t strip = index / kPerStrip;
        m_volumes[strip].start(m_store.number(index), samplesOf(m_store.number(indexOf(strip, Ramp))));
    }
}

std::expected<void, BlockError> Mixer::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue Mixer::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap Mixer::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> Mixer::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

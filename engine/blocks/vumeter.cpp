// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/vumeter.h"

#include "dsp/decibel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;

/// A full-wave rectified sine averages 2/π of its peak; this puts it back,
/// so a steady sine reads its own peak.
constexpr double kSineCalibration = std::numbers::pi / 2.0;

/// Below this a needle and its speed are both at rest. Far under anything
/// the scale can show, and well above where a double turns denormal.
constexpr double kAtRest = 1.0e-12;

/// The reference choices in the order the descriptor lists them, as dBFS.
constexpr std::array kReferenceDb{-20.0f, -18.0f, -14.0f};

} // namespace

std::vector<ParamDescriptor> VuMeter::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor reference;
    reference.id = "reference";
    reference.type = ParamType::Enum;
    reference.choices = {std::string{VuMeter::kReferenceMinus20},
                         std::string{VuMeter::kReferenceMinus18},
                         std::string{VuMeter::kReferenceMinus14}};
    // −18 dBFS, the level a digital studio most often lines 0 VU up with.
    reference.defaultValue = ParamValue::ofEnum(std::string{VuMeter::kReferenceMinus18});
    descriptors.push_back(std::move(reference));

    ParamDescriptor tray;
    tray.id = "showInTray";
    tray.type = ParamType::Bool;
    tray.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(tray));
    return descriptors;
}

VuMeter::VuMeter()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Meters;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Vu;
    // Nothing, for the same reason as the Peak/RMS block: a meter that
    // delayed the signal would have to be compensated for everywhere.
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> VuMeter::make()
{
    return std::make_unique<VuMeter>();
}

std::expected<void, BlockError> VuMeter::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_dt = 1.0 / spec.sampleRate;
    m_meter = spec.meter;
    reset();
    return {};
}

void VuMeter::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    constexpr double stiffness = kNaturalRadPerSecond * kNaturalRadPerSecond;
    constexpr double friction = 2.0 * kDamping * kNaturalRadPerSecond;

    const std::uint32_t channels = std::min(context.output.channelCount(), context.input.channelCount());
    for (std::uint32_t channel = 0; channel < channels && channel < kChannels; ++channel) {
        const std::span<float> in = context.input.channel(channel);
        const std::span<float> out = context.output.channel(channel);
        double position = m_position[channel];
        double velocity = m_velocity[channel];
        for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
            const float sample = frame < in.size() ? in[frame] : 0.0f;
            if (frame < out.size()) {
                // **Sample for sample.** A meter is something to look at,
                // not something in the way.
                out[frame] = sample;
            }
            // The needle, one sample at a time: pulled towards the
            // rectified signal, held back by its damping. Speed first and
            // then position (semi-implicit Euler), which is stable at any
            // audio rate for a system this slow.
            const double drive = kSineCalibration * std::abs(static_cast<double>(sample));
            velocity += m_dt * (stiffness * (drive - position) - friction * velocity);
            position += m_dt * velocity;
        }
        if (std::abs(position) < kAtRest && std::abs(velocity) < kAtRest) {
            position = 0.0;
            velocity = 0.0;
        }
        m_position[channel] = position;
        m_velocity[channel] = velocity;
    }

    if (m_meter != nullptr) {
        m_meter->publish(kLevelLeftValue, level(0));
        m_meter->publish(kLevelRightValue, level(1));
    }
}

float VuMeter::level(std::uint32_t channel) const noexcept RB_NONBLOCKING
{
    if (channel >= kChannels) {
        return 0.0f;
    }
    // A needle swings past zero on its way back from a loud passage; the
    // pin at the bottom of the scale stops it there.
    return static_cast<float>(std::max(0.0, m_position[channel]));
}

float VuMeter::referenceDb() const noexcept RB_NONBLOCKING
{
    const auto choice = static_cast<std::size_t>(std::lround(m_store.number(Reference)));
    return kReferenceDb[std::min(choice, kReferenceDb.size() - 1)];
}

float VuMeter::vu(std::uint32_t channel) const noexcept RB_NONBLOCKING
{
    return gainToDb(level(channel)) - referenceDb();
}

void VuMeter::reset() noexcept RB_NONBLOCKING
{
    m_position.fill(0.0);
    m_velocity.fill(0.0);
}

void VuMeter::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> VuMeter::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue VuMeter::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap VuMeter::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> VuMeter::loadState(const ParamValueMap &state)
{
    return m_store.load(state);
}

} // namespace rb::blocks

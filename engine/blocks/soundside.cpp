// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/soundside.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kChannels = 2;
constexpr std::uint32_t kLeft = 0;
constexpr std::uint32_t kRight = 1;

constexpr float kDefaultGateDb = -50.0f;
constexpr float kDefaultFrom = 1000.0f;
constexpr float kDefaultTo = 6000.0f;
/// Keeps every sum defined with nothing on one side.
constexpr double kFloor = 1.0e-12;

[[nodiscard]] ParamDescriptor numberParam(const char *id, float low, float high, float fallback)
{
    ParamDescriptor descriptor;
    descriptor.id = id;
    descriptor.type = ParamType::Float;
    descriptor.minimum = ParamValue::ofFloat(low);
    descriptor.maximum = ParamValue::ofFloat(high);
    descriptor.defaultValue = ParamValue::ofFloat(fallback);
    return descriptor;
}

} // namespace

SoundSide::BandEdges SoundSide::octaveApart(float from, float to, bool toMoved) noexcept RB_NONBLOCKING
{
    // The ranges are an octave apart at both ends, so neither has to give way
    // past its own limit.
    if (toMoved) {
        return {std::min(from, to / kLeastBandRatio), to};
    }
    return {from, std::max(to, from * kLeastBandRatio)};
}

std::vector<ParamDescriptor> SoundSide::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;
    descriptors.push_back(numberParam("gateDb", -70.0f, -20.0f, kDefaultGateDb));

    ParamDescriptor band;
    band.id = "band";
    band.type = ParamType::Enum;
    band.choices = {"all", "range"};
    band.defaultValue = ParamValue::ofEnum("all");
    descriptors.push_back(std::move(band));

    ParamDescriptor from = numberParam("fromHz", kLowestFrom, kHighestFrom, kDefaultFrom);
    from.taper = ParamTaper::Logarithmic;
    descriptors.push_back(std::move(from));

    ParamDescriptor to = numberParam("toHz", kLowestTo, kHighestTo, kDefaultTo);
    to.taper = ParamTaper::Logarithmic;
    descriptors.push_back(std::move(to));

    // Where a thing is shown is no part of how it reads, so a preset leaves
    // this as it is: a strip must not appear because of one.
    ParamDescriptor strip;
    strip.id = "showStrip";
    strip.type = ParamType::Bool;
    strip.defaultValue = ParamValue::ofBool(false);
    strip.flags.inPresets = false;
    descriptors.push_back(std::move(strip));
    return descriptors;
}

SoundSide::SoundSide()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Meters;
    m_info.gainReach = GainReach::Unity;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    m_info.latencySamples = 0;
}

std::unique_ptr<Block> SoundSide::make()
{
    return std::make_unique<SoundSide>();
}

std::expected<void, BlockError> SoundSide::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_hopFrames =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(spec.sampleRate * kHopMs / 1000.0)));
    m_meter = spec.meter;
    m_edges = {0.0f, 0.0f}; // Worked out again for this rate.
    reset();
    return {};
}

SoundSide::BandEdges SoundSide::bandInUse() const noexcept RB_NONBLOCKING
{
    return octaveApart(m_store.number(FromHz), m_store.number(ToHz), m_toMovedLast.load(std::memory_order_relaxed));
}

void SoundSide::followBand() noexcept RB_NONBLOCKING
{
    m_bandOn = m_store.number(Band) != 0.0;
    const BandEdges edges = bandInUse();
    if (edges.from == m_edges.from && edges.to == m_edges.to) {
        return;
    }
    m_edges = edges;
    FilterSpec high;
    high.shape = FilterShape::HighPass;
    high.frequency = edges.from;
    m_highPass = designSvf(high, m_sampleRate);
    FilterSpec low;
    low.shape = FilterShape::LowPass;
    low.frequency = edges.to;
    m_lowPass = designSvf(low, m_sampleRate);
}

void SoundSide::closeHop() noexcept RB_NONBLOCKING
{
    m_ring[m_ringNext] = m_hop;
    m_ringNext = (m_ringNext + 1) % kWindowHops;
    m_hop = {};
    m_hopFilled = 0;

    Energies window;
    for (const Energies &hop : m_ring) {
        window.left += hop.left;
        window.right += hop.right;
        window.both += hop.both;
    }
    const double energy = window.left + window.right;
    const double frames = static_cast<double>(m_hopFrames) * kWindowHops;
    m_level = static_cast<float>(10.0 * std::log10(energy / (2.0 * frames) + kFloor));
    m_heldAgeMs = std::min(m_heldAgeMs + kHopMs, kHoldMs);
    const bool holdIsOver = m_heldAgeMs >= kHoldMs && m_heldPublished;

    if (m_level >= m_store.number(GateDb)) {
        if (energy >= m_heldEnergy || holdIsOver) {
            m_heldEnergy = energy;
            m_heldAgeMs = 0;
            m_heldPublished = false;
            m_heldLevel = m_level;
            m_pan = static_cast<float>((window.right - window.left) / (energy + kFloor));
            m_width = static_cast<float>(window.both / std::sqrt(window.left * window.right + kFloor));
            m_state = Sounding;
        }
        const bool alike = std::abs(m_pan) < kAlikePan && m_width > kAlikeWidth;
        m_alikeMs = alike ? std::min(m_alikeMs + kHopMs, kAlikeMs) : 0;
        if (m_alikeMs >= kAlikeMs) {
            m_state = SidesAlike;
        }
    } else if (holdIsOver) {
        m_heldEnergy = 0.0;
        m_state = Quiet;
    }
}

void SoundSide::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    followBand();
    const bool stereoIn = context.input.channelCount() >= kChannels;
    const bool stereoOut = context.output.channelCount() >= kChannels;
    const std::span<float> inLeft = context.input.channel(kLeft);
    const std::span<float> inRight = stereoIn ? context.input.channel(kRight) : inLeft;
    const std::span<float> outLeft = context.output.channel(kLeft);
    const std::span<float> outRight = stereoOut ? context.output.channel(kRight) : outLeft;
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        // Both read before either is written: the buffers can be shared.
        float left = inLeft[frame];
        float right = inRight[frame];
        outLeft[frame] = left;
        outRight[frame] = right;
        if (m_bandOn) {
            left = m_lowPassSide[kLeft].process(m_highPassSide[kLeft].process(left, m_highPass), m_lowPass);
            right = m_lowPassSide[kRight].process(m_highPassSide[kRight].process(right, m_highPass), m_lowPass);
        }
        m_hop.left += static_cast<double>(left) * left;
        m_hop.right += static_cast<double>(right) * right;
        m_hop.both += static_cast<double>(left) * right;
        if (++m_hopFilled == m_hopFrames) {
            closeHop();
        }
    }
    m_heldPublished = true;
    if (m_meter != nullptr) {
        m_meter->publish(kPanValue, m_pan);
        m_meter->publish(kLevelValue, level());
        m_meter->publish(kWidthValue, m_width);
        m_meter->publish(kStateValue, static_cast<float>(m_state));
    }
}

void SoundSide::reset() noexcept RB_NONBLOCKING
{
    followBand();
    for (Svf &side : m_highPassSide) {
        side.reset();
    }
    for (Svf &side : m_lowPassSide) {
        side.reset();
    }
    m_hop = {};
    m_hopFilled = 0;
    m_ring.fill({});
    m_ringNext = 0;
    m_heldEnergy = 0.0;
    m_heldAgeMs = kHoldMs;
    m_heldPublished = true;
    m_alikeMs = 0;
    m_pan = 0.0f;
    m_width = 0.0f;
    m_level = kNoLevel;
    m_heldLevel = kNoLevel;
    m_state = Quiet;
}

void SoundSide::switchedOn() noexcept RB_NONBLOCKING
{
    // What it held when it was switched off is long out of date.
    reset();
}

void SoundSide::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    if (index == FromHz || index == ToHz) {
        m_toMovedLast.store(index == ToHz, std::memory_order_relaxed);
    }
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> SoundSide::setParam(std::string_view id, const ParamValue &value)
{
    if (id == "fromHz" || id == "toHz") {
        m_toMovedLast.store(id == "toHz", std::memory_order_relaxed);
    }
    return m_store.set(id, value);
}

ParamValue SoundSide::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap SoundSide::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> SoundSide::loadState(const ParamValueMap &state)
{
    m_toMovedLast.store(false, std::memory_order_relaxed);
    return m_store.load(state);
}

} // namespace rb::blocks

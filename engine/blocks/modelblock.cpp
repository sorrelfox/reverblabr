// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/modelblock.h"

#include "dsp/audioblock.h"
#include "rt/meter.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

/// The spare time's choices, in milliseconds, in the order of their ids.
constexpr std::array<double, 4> kSpareMs{20.0, 50.0, 100.0, 250.0};
constexpr std::uint32_t kDefaultSpare = 1;

/// A number for a mix of text: the same inputs, the same shape, as a fresh
/// block with the same settings has to say.
[[nodiscard]] std::uint32_t mixed(std::uint32_t hash, std::string_view text) noexcept
{
    for (const char c : text) {
        hash = (hash ^ static_cast<std::uint8_t>(c)) * 16777619U;
    }
    return hash;
}

[[nodiscard]] std::uint32_t mixed(std::uint32_t hash, std::uint32_t number) noexcept
{
    for (int shift = 0; shift < 32; shift += 8) {
        hash = (hash ^ ((number >> shift) & 0xFFU)) * 16777619U;
    }
    return hash;
}

} // namespace

std::vector<ParamDescriptor> ModelBlock::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    // **Applied by the block itself, on the thread that sets it**: asking
    // what a folder holds is work for the engine thread.
    ParamDescriptor folder;
    folder.id = "folder";
    folder.type = ParamType::String;
    folder.defaultValue = ParamValue::ofString(std::string{});
    folder.flags.hidden = true;
    descriptors.push_back(std::move(folder));

    ParamDescriptor spare;
    spare.id = "spareTime";
    spare.type = ParamType::Enum;
    spare.choices = {"20ms", "50ms", "100ms", "250ms"};
    spare.defaultValue = ParamValue::ofEnum(spare.choices[kDefaultSpare]);
    descriptors.push_back(std::move(spare));

    ParamDescriptor mono;
    mono.id = "monoMode";
    mono.type = ParamType::Enum;
    mono.choices = {"dualMono", "downmix"};
    mono.defaultValue = ParamValue::ofEnum(mono.choices.front());
    descriptors.push_back(std::move(mono));

    ParamDescriptor retry;
    retry.id = "retry";
    retry.type = ParamType::Int;
    retry.minimum = ParamValue::ofInt(0);
    retry.maximum = ParamValue::ofInt(1000000);
    retry.defaultValue = ParamValue::ofInt(0);
    retry.flags.momentary = true;
    retry.flags.hidden = true;
    retry.flags.inPresets = false;
    descriptors.push_back(std::move(retry));

    // Read before each of the model's runs, so a change needs no new host.
    ParamDescriptor semitones;
    semitones.id = "semitones";
    semitones.type = ParamType::Int;
    semitones.minimum = ParamValue::ofInt(-kMostSemitones);
    semitones.maximum = ParamValue::ofInt(kMostSemitones);
    semitones.defaultValue = ParamValue::ofInt(0);
    semitones.unit = ParamUnit::Semitones;
    descriptors.push_back(std::move(semitones));
    return descriptors;
}

ModelBlock::ModelBlock()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(model::kRingChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(model::kRingChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    m_info.silentWhenOff = true;
}

ModelBlock::~ModelBlock()
{
    letGo();
}

std::unique_ptr<Block> ModelBlock::make()
{
    return std::make_unique<ModelBlock>();
}

void ModelBlock::letGo() noexcept
{
    m_link.reset();
    m_rings.reset();
    m_inputRing = {};
    m_outputRing = {};
    m_mask = 0;
}

void ModelBlock::inspect()
{
    const std::string folder = m_store.value(Folder).text();
    if (folder == m_inspected) {
        return;
    }
    m_inspected = folder;
    m_description = model::Description{};
    if (!folder.empty()) {
        m_description = inspectFolder(defaultModelHostProgram(), folder);
    }
    m_hasFolder.store(!folder.empty(), std::memory_order_relaxed);
    m_refusal.store(m_description.refusal, std::memory_order_relaxed);
}

std::uint32_t ModelBlock::spareFrames(double sampleRate) const noexcept
{
    const auto choice = std::min<std::size_t>(static_cast<std::size_t>(m_store.number(Spare)), kSpareMs.size() - 1);
    return static_cast<std::uint32_t>(std::lround(kSpareMs[choice] * sampleRate / 1000.0));
}

std::uint32_t ModelBlock::preparedShape() const
{
    const std::string folder = m_store.value(Folder).text();
    if (folder.empty()) {
        return 0;
    }
    std::uint32_t hash = 2166136261U;
    hash = mixed(hash, folder);
    hash = mixed(hash, static_cast<std::uint32_t>(m_description.refusal));
    hash = mixed(hash, m_description.modelFile);
    hash = mixed(hash, m_description.sampleRate);
    hash = mixed(hash, m_description.rateIn());
    hash = mixed(hash, m_description.hopSize);
    hash = mixed(hash, m_description.latencySamples);
    hash = mixed(hash, m_description.runMs);
    hash = mixed(hash, static_cast<std::uint32_t>(m_store.number(Spare)));
    hash = mixed(hash, static_cast<std::uint32_t>(m_store.number(Mono)));
    // Zero says "nothing to prepare for", which a folder never does.
    return hash == 0 ? 1 : hash;
}

std::expected<void, BlockError> ModelBlock::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    letGo();
    m_sampleRate = spec.sampleRate;
    m_meter = spec.meter;
    m_prepared = true;
    m_written = 0;
    m_floor = 0;
    m_gate = 0.0f;
    m_gateStep = 0.0f;
    m_healthy = false;
    m_delivered = false;
    m_cycleTooLong = false;
    m_lastMiss = 0;
    m_lastBad = 0;
    m_badRunsSeen = 0;
    m_prepareRefusal = model::Refusal::None;
    m_latency = 0;
    m_info.latencySamples = 0;
    m_guard = static_cast<std::uint32_t>(std::lround(kGuardMs * spec.sampleRate / 1000.0));
    m_shownFrames = static_cast<std::uint64_t>(kShownSeconds * spec.sampleRate);

    if (!m_description.usable() || m_store.value(Folder).text().empty()) {
        return {};
    }
    const auto rate = static_cast<std::uint32_t>(std::lround(spec.sampleRate));
    const std::uint32_t spare = spareFrames(spec.sampleRate);
    const std::optional<model::Timing> timing = model::timingFor(m_description, rate, spare);
    if (!timing) {
        m_prepareRefusal = model::Refusal::RateNotUsable;
        return {};
    }
    std::expected<model::SharedRings, model::SharedError> rings =
        model::SharedRings::create(2 * (timing->latency + kMaxBlockFrames));
    if (!rings) {
        m_prepareRefusal = model::Refusal::NoHost;
        return {};
    }
    m_rings.emplace(std::move(*rings));
    m_latency = timing->latency;
    m_info.latencySamples = timing->latency;
    m_longestCycle = spare > m_guard ? spare - m_guard : 1;
    m_mask = m_rings->capacity() - 1;
    for (std::uint32_t channel = 0; channel < model::kRingChannels; ++channel) {
        m_inputRing[channel] = m_rings->input(channel);
        m_outputRing[channel] = m_rings->output(channel);
    }
    ModelHostSpec host;
    host.program = defaultModelHostProgram();
    host.folder = m_store.value(Folder).text();
    host.sampleRate = rate;
    host.latency = timing->latency;
    host.downmix = m_store.number(Mono) >= 0.5f;
    m_link = std::make_unique<ModelHostLink>(*m_rings, std::move(host));
    return {};
}

void ModelBlock::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t frames = context.frames;
    std::array<std::span<const float>, model::kRingChannels> in;
    std::array<std::span<float>, model::kRingChannels> out;
    for (std::uint32_t channel = 0; channel < model::kRingChannels; ++channel) {
        const std::span<float> arriving = context.input.channel(channel);
        in[channel] = {arriving.data(), arriving.size()};
        out[channel] = context.output.channel(channel);
    }

    if (m_mask == 0) {
        for (const std::span<float> channel : out) {
            std::ranges::fill(channel, 0.0f);
        }
        publish();
        return;
    }

    // **In first**: the graph may hand one buffer for both, and the output
    // below must never hold what came in.
    const std::uint64_t first = m_written;
    for (std::uint32_t channel = 0; channel < model::kRingChannels; ++channel) {
        float *const ring = m_inputRing[channel].data();
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            ring[(first + frame) & m_mask] = frame < in[channel].size() ? in[channel][frame] : 0.0f;
        }
    }
    m_written = first + frames;
    model::SharedHeader &header = m_rings->header();
    header.pitchCents.store(static_cast<std::int32_t>(std::lround(m_store.number(Semitones) * 100.0f)),
                            std::memory_order_relaxed);
    header.written.store(m_written, std::memory_order_release);

    // What the host says is believed only as far as it can be true.
    const auto phase = static_cast<model::Phase>(header.phase.load(std::memory_order_acquire));
    const bool ready = phase == model::Phase::Ready && m_link->failure() == model::Refusal::None;
    const std::uint64_t wetFrom = std::max(header.wetFrom.load(std::memory_order_acquire), m_floor);
    const std::uint64_t wetEnd = std::min(header.wetEnd.load(std::memory_order_acquire), m_written);
    if (const std::uint64_t badRuns = header.badRuns.load(std::memory_order_relaxed); badRuns != m_badRunsSeen) {
        m_badRunsSeen = badRuns;
        m_lastBad = m_written;
    }

    // The frames of this cycle before the first ever were silence going in.
    const std::uint64_t start = first > m_latency ? first - m_latency : 0;
    const std::uint32_t lead =
        first >= m_latency ? 0 : static_cast<std::uint32_t>(std::min<std::uint64_t>(m_latency - first, frames));
    std::uint64_t have = lead;
    if (ready && start >= wetFrom && wetEnd > start) {
        have += wetEnd - start;
    }
    m_cycleTooLong = frames > m_longestCycle;
    const bool healthy = ready && have >= std::uint64_t{frames} + m_guard;
    m_delivered = ready && (m_delivered || (lead < frames && start >= wetFrom));
    // Nothing has been heard yet: there is nothing to fade from.
    if (healthy && lead == frames) {
        m_gate = 1.0f;
    }
    if (!healthy && m_healthy && m_delivered) {
        m_lastMiss = m_written;
    }
    m_healthy = healthy;

    // Down over the sound still in hand, up over the guard's length.
    const auto usable = static_cast<std::uint32_t>(std::min<std::uint64_t>(have, frames));
    const float rise = 1.0f / static_cast<float>(m_guard);
    const float fall = m_gate / static_cast<float>(std::max<std::uint32_t>(1, std::min(m_guard, usable)));
    bool bad = false;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        m_gate = healthy ? std::min(1.0f, m_gate + rise) : std::max(0.0f, m_gate - fall);
        const bool inHand = frame >= lead && frame < usable;
        for (std::uint32_t channel = 0; channel < model::kRingChannels; ++channel) {
            if (frame >= out[channel].size()) {
                continue;
            }
            float sample = 0.0f;
            if (inHand) {
                sample = m_outputRing[channel].data()[(start + (frame - lead)) & m_mask];
                if (!std::isfinite(sample)) {
                    sample = 0.0f;
                    bad = true;
                }
            }
            out[channel][frame] = std::clamp(sample, -kHighestSample, kHighestSample) * m_gate;
        }
    }
    if (bad) {
        // The host silences what it sees; this is for a host that does not.
        m_gate = 0.0f;
        m_lastBad = m_written;
    }

    const auto presses = static_cast<std::uint32_t>(m_store.number(Retry));
    if (presses != m_retriesSeen) {
        m_retriesSeen = presses;
        m_link->tryAgain();
    }
    publish();
}

ModelBlock::State ModelBlock::state() const noexcept RB_NONBLOCKING
{
    if (!m_hasFolder.load(std::memory_order_relaxed)) {
        return NoModel;
    }
    if (m_prepareRefusal != model::Refusal::None || m_refusal.load(std::memory_order_relaxed) != model::Refusal::None) {
        return Failed;
    }
    if (m_link == nullptr) {
        return Loading;
    }
    if (m_link->failure() != model::Refusal::None) {
        return Failed;
    }
    if (m_cycleTooLong) {
        return CannotKeepUp;
    }
    if (m_link->restarting()) {
        return Restarting;
    }
    if (static_cast<model::Phase>(m_rings->header().phase.load(std::memory_order_acquire)) != model::Phase::Ready
        || !m_delivered) {
        return Loading;
    }
    if (m_lastBad != 0 && m_written - m_lastBad < m_shownFrames) {
        return BadOutput;
    }
    if (!m_healthy || (m_lastMiss != 0 && m_written - m_lastMiss < m_shownFrames)) {
        return CannotKeepUp;
    }
    return Running;
}

std::uint32_t ModelBlock::detail() const noexcept RB_NONBLOCKING
{
    const State now = state();
    if (now == Failed) {
        if (m_prepareRefusal != model::Refusal::None) {
            return static_cast<std::uint32_t>(m_prepareRefusal);
        }
        if (const model::Refusal refusal = m_refusal.load(std::memory_order_relaxed); refusal != model::Refusal::None) {
            return static_cast<std::uint32_t>(refusal);
        }
        return m_link != nullptr ? static_cast<std::uint32_t>(m_link->failure()) : 0;
    }
    if (now == CannotKeepUp && m_cycleTooLong) {
        return static_cast<std::uint32_t>(model::Refusal::CycleTooLong);
    }
    if (now == Running && m_rings) {
        return m_rings->header().device.load(std::memory_order_relaxed);
    }
    return 0;
}

void ModelBlock::publish() noexcept RB_NONBLOCKING
{
    if (m_meter == nullptr) {
        return;
    }
    m_meter->publish(kStateValue, static_cast<float>(state()));
    m_meter->publish(kLatencyMsValue, static_cast<float>(static_cast<double>(m_latency) * 1000.0 / m_sampleRate));
    m_meter->publish(kLoadValue,
                     m_rings ? static_cast<float>(m_rings->header().load.load(std::memory_order_relaxed)) / 10.0f
                             : 0.0f);
    m_meter->publish(kDetailValue, static_cast<float>(detail()));
}

std::uint64_t ModelBlock::wetEnd() const noexcept
{
    return m_rings ? m_rings->header().wetEnd.load(std::memory_order_acquire) : 0;
}

int ModelBlock::hostPid() const noexcept
{
    return m_link != nullptr ? m_link->pid() : -1;
}

bool ModelBlock::hostReady() const noexcept
{
    return m_rings
        && static_cast<model::Phase>(m_rings->header().phase.load(std::memory_order_acquire)) == model::Phase::Ready;
}

void ModelBlock::reset() noexcept RB_NONBLOCKING
{
    m_gate = 0.0f;
    m_healthy = false;
    // What the model made of the old path is no answer to the new one.
    m_floor = m_written;
    if (m_rings) {
        m_rings->header().resetEpoch.fetch_add(1, std::memory_order_relaxed);
    }
}

void ModelBlock::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> ModelBlock::setParam(std::string_view id, const ParamValue &value)
{
    const std::expected<void, BlockError> set = m_store.set(id, value);
    if (set && id == "folder") {
        inspect();
    }
    return set;
}

ParamValue ModelBlock::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap ModelBlock::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> ModelBlock::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded) {
        inspect();
    }
    return loaded;
}

} // namespace rb::blocks

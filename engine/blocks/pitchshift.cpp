// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/pitchshift.h"

#include "rt/meter.h"

#include <SoundTouch.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <map>
#include <mutex>
#include <numbers>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

// The block hands SoundTouch floats. A distribution that builds it for 16-bit
// integers instead would need a conversion on the way in and out.
static_assert(std::is_same_v<soundtouch::SAMPLETYPE, float>, "SoundTouch has to be built for float samples");

/// One SoundTouch set to one pitch, and what it has given back so far.
struct PitchEngine {
    soundtouch::SoundTouch touch;
    float semitones = 0.0f;
    std::uint32_t channels = PitchShift::kChannels;
    /// What SoundTouch has given back, one ring per channel, capacity frames
    /// long (a power of two): frame j of its output is at j & mask.
    std::vector<float> ring;
    std::size_t capacity = 0;
    std::size_t mask = 0;
    /// What one receive is unpacked through, interleaved.
    std::vector<float> received;
    /// How many frames SoundTouch has given back since it started.
    std::uint64_t produced = 0;
    /// The block's frame at which this engine was given its first input:
    /// output frame j of the engine is the sound of input frame start + j.
    std::uint64_t start = 0;
};

namespace
{

/// **SoundTouch from the audio thread.** The compiler cannot see into the
/// library, so it cannot check it: -Wfunction-effects is quietened for these
/// three calls and nothing else. What the check cannot see, the rtsan preset
/// does at run time, and the allocation trap in the tests: once makeEngine()
/// has grown every buffer, none of them allocates, locks or waits.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
#endif
void putInto(soundtouch::SoundTouch &touch, const float *frames, unsigned int count) noexcept RB_NONBLOCKING
{
    touch.putSamples(frames, count);
}

[[nodiscard]] unsigned int
takeFrom(soundtouch::SoundTouch &touch, float *frames, unsigned int most) noexcept RB_NONBLOCKING
{
    return touch.receiveSamples(frames, most);
}

void forget(soundtouch::SoundTouch &touch) noexcept RB_NONBLOCKING
{
    touch.clear();
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

/// Sets up a SoundTouch as the block runs it.
void configure(soundtouch::SoundTouch &touch, double sampleRate, std::uint32_t channels, float semitones)
{
    touch.setSampleRate(static_cast<unsigned int>(std::lround(sampleRate)));
    touch.setChannels(channels);
    touch.setSetting(SETTING_USE_AA_FILTER, 1);
    touch.setSetting(SETTING_SEQUENCE_MS, PitchShift::kSequenceMs);
    touch.setSetting(SETTING_SEEKWINDOW_MS, PitchShift::kSeekWindowMs);
    touch.setSetting(SETTING_OVERLAP_MS, PitchShift::kOverlapMs);
    touch.setPitchSemiTones(semitones);
}

/// The longest SoundTouch keeps the output waiting at \a sampleRate, fed in
/// pieces of kChunk from the start: after each piece, how many input frames
/// have not come back yet, at its worst, over every pitch in the range.
std::uint32_t measureLatency(double sampleRate)
{
    const auto piece = PitchShift::kChunk;
    const auto frames = static_cast<std::uint64_t>(sampleRate * 0.6);
    std::vector<float> silence(piece, 0.0f);
    std::vector<float> out(piece);
    std::uint64_t worst = 0;
    const auto widest = static_cast<int>(PitchShift::kMaxSemitones + 1);
    for (int semitones = -widest; semitones <= widest; ++semitones) {
        soundtouch::SoundTouch touch;
        configure(touch, sampleRate, 1, static_cast<float>(semitones));
        std::uint64_t in = 0;
        std::uint64_t given = 0;
        while (in < frames) {
            touch.putSamples(silence.data(), piece);
            in += piece;
            unsigned int got = 0;
            while ((got = touch.receiveSamples(out.data(), piece)) > 0) {
                given += got;
            }
            worst = std::max(worst, in > given ? in - given : 0);
        }
    }
    // A frame is read once the piece it belongs to has gone in, which can be
    // one piece after the frame itself arrived.
    return static_cast<std::uint32_t>(worst + piece);
}

} // namespace

std::uint32_t PitchShift::latencyFor(double sampleRate)
{
    static std::mutex lock;
    static std::map<long, std::uint32_t> known;
    const long rate = std::lround(sampleRate);
    const std::scoped_lock guard(lock);
    if (const auto found = known.find(rate); found != known.end()) {
        return found->second;
    }
    const std::uint32_t latency = measureLatency(sampleRate);
    known.emplace(rate, latency);
    return latency;
}

std::vector<ParamDescriptor> PitchShift::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor semitones;
    semitones.id = "semitones";
    semitones.type = ParamType::Int;
    semitones.minimum = ParamValue::ofInt(-kMaxSemitones);
    semitones.maximum = ParamValue::ofInt(kMaxSemitones);
    semitones.defaultValue = ParamValue::ofInt(0);
    semitones.unit = ParamUnit::Semitones;
    descriptors.push_back(std::move(semitones));

    ParamDescriptor cents;
    cents.id = "cents";
    cents.type = ParamType::Float;
    cents.minimum = ParamValue::ofFloat(-kMaxCents);
    cents.maximum = ParamValue::ofFloat(kMaxCents);
    cents.defaultValue = ParamValue::ofFloat(0.0f);
    cents.unit = ParamUnit::Cents;
    descriptors.push_back(std::move(cents));
    return descriptors;
}

PitchShift::PitchShift()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Effects;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Value;
    // Set at prepare(), once the rate is known: working it out takes a run of
    // SoundTouch, which a block built only to be listed should not pay for.
    m_info.latencySamples = 0;
}

PitchShift::~PitchShift()
{
    delete m_pending.exchange(nullptr, std::memory_order_acq_rel);
    delete m_engine;
    delete m_incoming;
}

std::unique_ptr<Block> PitchShift::make()
{
    return std::make_unique<PitchShift>();
}

float PitchShift::shift() const noexcept RB_NONBLOCKING
{
    return m_store.number(Semitones) + (m_store.number(Cents) / 100.0f);
}

std::unique_ptr<PitchEngine> PitchShift::makeEngine(float semitones) const
{
    auto engine = std::make_unique<PitchEngine>();
    engine->semitones = semitones;
    engine->channels = kChannels;
    configure(engine->touch, m_sampleRate, kChannels, semitones);
    // Room for what has come back and not been heard yet: the latency, and a
    // burst of a whole piece of SoundTouch's on top of it.
    engine->capacity = std::bit_ceil(static_cast<std::size_t>(m_latency) * 2 + (kChunk * 4));
    engine->mask = engine->capacity - 1;
    engine->ring.assign(engine->capacity * kChannels, 0.0f);
    engine->received.assign(static_cast<std::size_t>(kChunk) * kChannels, 0.0f);

    // **Grown to its full size here, not on the audio thread.** What
    // SoundTouch keeps inside grows on first use, and how far depends only on
    // how many frames go in and out in each step, never on what they sound
    // like; running silence through it the way the audio thread will, for
    // several of its pieces, takes every buffer to the size it keeps.
    // clear() then forgets the silence and keeps the room.
    const std::vector<float> silence(static_cast<std::size_t>(kChunk) * kChannels, 0.0f);
    const auto warmFrames = static_cast<std::uint64_t>(m_sampleRate) + (static_cast<std::uint64_t>(m_latency) * 4);
    for (std::uint64_t fed = 0; fed < warmFrames; fed += kChunk) {
        engine->touch.putSamples(silence.data(), kChunk);
        while (engine->touch.receiveSamples(engine->received.data(), kChunk) > 0) { }
    }
    engine->touch.clear();
    return engine;
}

void PitchShift::collect()
{
    (void)m_retired.drain();
}

void PitchShift::rebuild()
{
    collect();
    // One the audio thread never took was never heard: this one replaces it.
    delete m_pending.exchange(makeEngine(shift()).release(), std::memory_order_acq_rel);
}

std::expected<void, BlockError> PitchShift::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_meter = spec.meter;
    m_latency = latencyFor(m_sampleRate);
    m_info.latencySamples = m_latency;

    // Nothing runs the block while it is prepared: whatever was on its way
    // is out of date, and the engine is put in place directly.
    collect();
    delete m_pending.exchange(nullptr, std::memory_order_acq_rel);
    delete m_incoming;
    m_incoming = nullptr;
    delete m_engine;
    m_engine = makeEngine(shift()).release();
    m_fadePosition = 0;

    m_chunk.assign(static_cast<std::size_t>(kChunk) * kChannels, 0.0f);
    m_fill = 0;
    m_frame = 0;
    m_missed = 0;
    m_prepared = true;
    return {};
}

void PitchShift::takePending() noexcept RB_NONBLOCKING
{
    // Room for the engines this may retire, or it waits: freeing one here is
    // what the queue is for not doing.
    if (m_incoming != nullptr || m_retired.pending() + 2 > m_retired.capacity()) {
        return;
    }
    PitchEngine *const next = m_pending.exchange(nullptr, std::memory_order_acq_rel);
    if (next == nullptr) {
        return;
    }
    // The piece about to go in is the new engine's first.
    next->start = m_frame + 1 - kChunk;
    // **Nothing heard from the old one yet** — the block has only just
    // started, or was reset — so there is nothing to cross over from.
    if (m_engine == nullptr || m_frame < m_engine->start + m_latency) {
        if (m_retired.tryRetire(m_engine)) {
            m_engine = next;
            return;
        }
    }
    m_incoming = next;
    m_fadePosition = 0;
}

void PitchShift::feedChunk() noexcept RB_NONBLOCKING
{
    for (PitchEngine *engine : {m_engine, m_incoming}) {
        if (engine == nullptr) {
            continue;
        }
        putInto(engine->touch, m_chunk.data(), kChunk);
        unsigned int got = 0;
        while ((got = takeFrom(engine->touch, engine->received.data(), kChunk)) > 0) {
            for (unsigned int frame = 0; frame < got; ++frame) {
                const std::size_t at = static_cast<std::size_t>(engine->produced + frame) & engine->mask;
                for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
                    engine->ring[(channel * engine->capacity) + at] =
                        engine->received[(static_cast<std::size_t>(frame) * kChannels) + channel];
                }
            }
            engine->produced += got;
        }
    }
}

float PitchShift::readEngine(const PitchEngine &engine, std::uint32_t channel) noexcept RB_NONBLOCKING
{
    // Output frame m_frame is the engine's frame m_frame - latency - start.
    if (m_frame < engine.start + m_latency) {
        return 0.0f;
    }
    const std::uint64_t wanted = m_frame - m_latency - engine.start;
    // The latency is the longest SoundTouch ever makes anyone wait, and the
    // ring holds more than that; both are checked rather than trusted.
    if (wanted >= engine.produced || engine.produced - wanted > engine.capacity) {
        if (channel == 0) {
            ++m_missed;
        }
        return 0.0f;
    }
    return engine.ring[(channel * engine.capacity) + (static_cast<std::size_t>(wanted) & engine.mask)];
}

void PitchShift::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::array<std::span<float>, kChannels> in{context.input.channel(0), context.input.channel(1)};
    const std::array<std::span<float>, kChannels> out{context.output.channel(0), context.output.channel(1)};
    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            m_chunk[(static_cast<std::size_t>(m_fill) * kChannels) + channel] =
                in[channel].empty() ? 0.0f : in[channel][frame];
        }
        if (++m_fill == kChunk) {
            takePending();
            feedChunk();
            m_fill = 0;
        }

        float fromOld = 1.0f;
        float fromNew = 0.0f;
        if (m_incoming != nullptr && m_frame >= m_incoming->start + m_latency + kSettleFrames) {
            // **Equal power**: the two pitches are different sounds, so they
            // add as powers, not as amplitudes. **Level at both ends**: the
            // way across goes through a raised cosine first, so neither
            // weight starts or stops with a slope, which a plain quarter sine
            // does and which the ear hears as a faint tick where the old
            // pitch leaves. Counted from one, so that on its last frame the
            // old pitch is at nothing.
            const float across = static_cast<float>(m_fadePosition + 1) / static_cast<float>(kFadeFrames);
            const float smooth = 0.5f - (0.5f * std::cos(across * std::numbers::pi_v<float>));
            fromOld = std::cos(smooth * std::numbers::pi_v<float> * 0.5f);
            fromNew = std::sin(smooth * std::numbers::pi_v<float> * 0.5f);
            if (m_fadePosition < kFadeFrames) {
                ++m_fadePosition;
            }
        }
        for (std::uint32_t channel = 0; channel < kChannels; ++channel) {
            if (out[channel].empty()) {
                continue;
            }
            float sample = m_engine != nullptr ? fromOld * readEngine(*m_engine, channel) : 0.0f;
            if (fromNew > 0.0f) {
                sample += fromNew * readEngine(*m_incoming, channel);
            }
            out[channel][frame] = sample;
        }
        ++m_frame;

        if (m_incoming != nullptr && m_fadePosition >= kFadeFrames && m_retired.tryRetire(m_engine)) {
            m_engine = std::exchange(m_incoming, nullptr);
            m_fadePosition = 0;
        }
    }
    if (m_meter != nullptr && m_engine != nullptr) {
        m_meter->publish(kShiftValue, m_engine->semitones);
    }
}

void PitchShift::reset() noexcept RB_NONBLOCKING
{
    // Nothing is left to cross over from.
    if (m_incoming != nullptr && m_retired.tryRetire(m_engine)) {
        m_engine = std::exchange(m_incoming, nullptr);
    }
    m_fadePosition = 0;
    for (PitchEngine *engine : {m_engine, m_incoming}) {
        if (engine != nullptr) {
            forget(engine->touch);
            engine->produced = 0;
            engine->start = 0;
        }
    }
    std::ranges::fill(m_chunk, 0.0f);
    m_fill = 0;
    m_frame = 0;
}

void PitchShift::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // The shift itself arrives as an engine built on the engine thread when
    // it was set; this keeps the number the block reports in step.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> PitchShift::setParam(std::string_view id, const ParamValue &value)
{
    const float before = shift();
    const std::expected<void, BlockError> set = m_store.set(id, value);
    if (set && m_prepared && shift() != before) {
        rebuild();
    }
    return set;
}

ParamValue PitchShift::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap PitchShift::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> PitchShift::loadState(const ParamValueMap &state)
{
    const float before = shift();
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared && shift() != before) {
        rebuild();
    }
    return loaded;
}

} // namespace rb::blocks

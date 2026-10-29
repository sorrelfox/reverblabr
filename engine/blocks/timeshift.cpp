// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "blocks/timeshift.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <utility>

using namespace rb::dsp;

namespace rb::blocks
{

namespace
{

constexpr std::uint32_t kLeft = 0;
constexpr std::uint32_t kRight = 1;
/// How far behind the file the audio thread's tap may run: seconds of audio
/// the spill's thread can be late by before anything is lost.
constexpr double kTapSeconds = 8.0;
/// How far ahead of the play head the spill reads.
constexpr double kFarSeconds = 8.0;
/// Each ring holds at least a hundred of the spill's sleeps: one late wake,
/// or a few, loses nothing.
static_assert(std::min(kTapSeconds, kFarSeconds) * 1000.0
              >= 100.0 * static_cast<double>(TimeShiftSpill::kPoll.count()));

} // namespace

std::vector<ParamDescriptor> TimeShift::makeDescriptors()
{
    std::vector<ParamDescriptor> descriptors;

    ParamDescriptor paused;
    paused.id = "paused";
    paused.type = ParamType::Bool;
    paused.defaultValue = ParamValue::ofBool(false);
    descriptors.push_back(std::move(paused));

    ParamDescriptor target;
    target.id = "target";
    target.type = ParamType::Float;
    target.minimum = ParamValue::ofFloat(0.0f);
    target.maximum = ParamValue::ofFloat(static_cast<float>(kLongestMinutes * 60));
    target.defaultValue = ParamValue::ofFloat(0.0f);
    target.unit = ParamUnit::None;
    target.flags.hidden = true;
    descriptors.push_back(std::move(target));

    // **A count, not a value**: jumping back 10 s twice is two jumps, and a
    // setting that already said 10 would not change the second time.
    ParamDescriptor jump;
    jump.id = "jump";
    jump.type = ParamType::Int;
    jump.minimum = ParamValue::ofInt(0);
    jump.maximum = ParamValue::ofInt(kMaxIntParamMagnitude);
    jump.defaultValue = ParamValue::ofInt(0);
    jump.flags.hidden = true;
    descriptors.push_back(std::move(jump));

    // How much is kept is how big the file and the rings are: a new length
    // builds the block afresh rather than reaching the audio thread.
    ParamDescriptor minutes;
    minutes.id = "minutes";
    minutes.type = ParamType::Int;
    minutes.minimum = ParamValue::ofInt(1);
    minutes.maximum = ParamValue::ofInt(kLongestMinutes);
    minutes.defaultValue = ParamValue::ofInt(kDefaultMinutes);
    minutes.flags.automatable = false;
    descriptors.push_back(std::move(minutes));
    return descriptors;
}

TimeShift::TimeShift()
    : m_descriptors(makeDescriptors())
    , m_store(m_descriptors)
    , m_spillDirectory(defaultSpillDirectory())
{
    m_info.typeId = std::string{kTypeId};
    m_info.category = BlockCategory::Advanced;
    m_info.inputs = PortSpec::fixed(kChannels, {"in_FL", "in_FR"});
    m_info.outputs = PortSpec::fixed(kChannels, {"out_FL", "out_FR"});
    m_info.face = FaceKind::Text;
    // **None**: at live the output is the input. The delay a person asks
    // for is intended, and reported through intendedDelaySamples().
    m_info.latencySamples = 0;
}

TimeShift::~TimeShift() = default;

std::unique_ptr<Block> TimeShift::make()
{
    return std::make_unique<TimeShift>();
}

std::filesystem::path TimeShift::defaultSpillDirectory()
{
    if (const char *cache = std::getenv("XDG_CACHE_HOME"); cache != nullptr && cache[0] != '\0') {
        return std::filesystem::path(cache) / "reverblabr" / "timeshift";
    }
    const char *home = std::getenv("HOME");
    return std::filesystem::path(home != nullptr ? home : "/tmp") / ".cache" / "reverblabr" / "timeshift";
}

void TimeShift::setSpillDirectory(std::filesystem::path directory)
{
    m_spillDirectory = std::move(directory);
}

void TimeShift::setSpillMode(TimeShiftSpill::Mode mode)
{
    m_spillMode = mode;
}

std::expected<void, BlockError> TimeShift::prepare(const PrepareSpec &spec)
{
    if (const std::expected<void, ParamListError> valid = validateParams(m_descriptors); !valid) {
        return std::unexpected(BlockError::InvalidDescriptor);
    }
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > kMaxBlockFrames) {
        return std::unexpected(BlockError::UnsupportedSpec);
    }
    m_sampleRate = spec.sampleRate;
    m_meter = spec.meter;
    const auto frames = [&](double seconds) {
        return static_cast<std::uint64_t>(std::llround(seconds * spec.sampleRate));
    };
    const std::uint64_t history = frames(static_cast<double>(m_store.number(Minutes)) * 60.0);
    const std::uint64_t tapFrames = frames(kTapSeconds);
    const std::uint64_t farFrames = frames(kFarSeconds);

    // Memory holds one frame more than the history it gives, the one being
    // written over as the oldest is read.
    const std::uint64_t memoryMost = frames(static_cast<double>(kMemoryMinutes) * 60.0);
    m_spill.reset();
    std::uint64_t memory = std::min(history, memoryMost);
    m_historyFrames = memory;
    if (history > memoryMost) {
        // The file holds the history and what may be on its way to it or
        // read ahead of the head: what is read is never what is being
        // written over. A spill that cannot make its file leaves as much
        // as memory alone keeps.
        auto spill = std::make_unique<TimeShiftSpill>(m_spillDirectory,
                                                      history + tapFrames + farFrames,
                                                      static_cast<std::size_t>(tapFrames),
                                                      static_cast<std::size_t>(farFrames),
                                                      m_spillMode);
        if (spill->ok()) {
            m_spill = std::move(spill);
            m_historyFrames = history;
            memory = frames(kRecentSeconds);
        }
    }
    m_ramFrames = memory + 1;
    m_ram.assign(static_cast<std::size_t>(m_ramFrames) * kChannels, 0.0f);

    m_fadeLength =
        std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(kFadeMs * 1.0e-3 * spec.sampleRate)));
    m_written = 0;
    m_head = 0;
    m_pending = false;
    m_dip = false;
    m_underruns = 0;
    m_seenJump = m_store.number(Jump);
    reset();
    m_prepared = true;
    return {};
}

std::uint64_t TimeShift::offsetSamples() const noexcept RB_NONBLOCKING
{
    // The head is the next position read, and the next position written is
    // m_written: that is how far behind live the next sample out will be.
    return m_written - std::min(m_head, m_written);
}

std::uint64_t TimeShift::availableSamples() const noexcept RB_NONBLOCKING
{
    return std::min(m_written, m_historyFrames);
}

std::uint32_t TimeShift::intendedDelaySamples() const noexcept RB_NONBLOCKING
{
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(offsetSamples(), 0xffffffffULL));
}

bool TimeShift::fetch(std::uint64_t position, float (&frame)[kChannels]) noexcept RB_NONBLOCKING
{
    if (position >= m_written) {
        return false;
    }
    if (!isFar(position)) {
        const std::size_t slot = static_cast<std::size_t>(position % m_ramFrames) * kChannels;
        frame[kLeft] = m_ram[slot];
        frame[kRight] = m_ram[slot + 1];
        return true;
    }
    if (m_spill != nullptr && m_spill->has(position)) {
        frame[kLeft] = m_spill->sample(position, kLeft);
        frame[kRight] = m_spill->sample(position, kRight);
        return true;
    }
    return false;
}

std::uint64_t TimeShift::pendingTarget() const noexcept RB_NONBLOCKING
{
    const std::uint64_t live = m_written - 1;
    return live - std::min({m_pendingOffset, live, m_historyFrames});
}

void TimeShift::tryJump() noexcept RB_NONBLOCKING
{
    const std::uint64_t target = pendingTarget();
    if (isFar(target)) {
        if (m_spill == nullptr) {
            m_pending = false;
            return;
        }
        const std::uint64_t last = target + m_fadeLength;
        if (!m_spill->covers(target) || !m_spill->covers(last)) {
            // **Asking moves the spill's ring**: a head playing from it
            // would lose its audio in the middle of a crossfade, so it dips
            // through silence instead, and asks once it is silent.
            if (isFar(m_head) && m_level > 0) {
                m_dip = true;
                return;
            }
            m_spill->request(target);
        }
        // Waits, playing on, until the spill has read far enough for the
        // crossfade too.
        if (!m_spill->has(target) || !m_spill->has(last)) {
            return;
        }
    }
    // **From wherever it was**: the old head fades out as the new one fades
    // in, both moving, so nothing steps. A silent head has nothing to fade.
    m_fadingFrom = m_head;
    m_fadeLeft = m_level > 0 ? m_fadeLength : 0;
    m_head = target;
    m_pending = false;
    m_dip = false;
}

void TimeShift::process(ProcessContext &context) noexcept RB_NONBLOCKING
{
    if (m_store.number(Jump) != m_seenJump) {
        m_seenJump = m_store.number(Jump);
        m_pendingOffset =
            static_cast<std::uint64_t>(std::llround(static_cast<double>(m_store.number(Target)) * m_sampleRate));
        m_pending = true;
    }
    const bool paused = m_store.number(Paused) != 0.0f;

    const std::span<float> leftIn = context.input.channel(kLeft);
    const std::span<float> rightIn = context.input.channel(kRight);
    const std::span<float> leftOut = context.output.channel(kLeft);
    const std::span<float> rightOut = context.output.channel(kRight);

    // Everything that comes in is kept: on its way to the file, a cycle at
    // a time, and in memory, a sample at a time.
    if (m_spill != nullptr) {
        const std::array<const float *, kChannels> channels{leftIn.empty() ? nullptr : leftIn.data(),
                                                            rightIn.empty() ? nullptr : rightIn.data()};
        m_spill->write(channels, context.frames);
    }

    for (std::uint32_t frame = 0; frame < context.frames; ++frame) {
        const std::size_t slot = static_cast<std::size_t>(m_written % m_ramFrames) * kChannels;
        m_ram[slot] = frame < leftIn.size() ? leftIn[frame] : 0.0f;
        m_ram[slot + 1] = frame < rightIn.size() ? rightIn[frame] : 0.0f;
        ++m_written;
        const std::uint64_t live = m_written - 1;

        if (m_pending) {
            tryJump();
        }
        // A head left behind for longer than there is history (paused too
        // long) is brought forward to the oldest sample still kept.
        if (live - std::min(m_head, live) > m_historyFrames) {
            m_head = live - m_historyFrames;
        }

        const bool silent = paused || m_dip;
        if (silent) {
            m_level -= m_level > 0 ? 1 : 0;
        } else {
            m_level += m_level < m_fadeLength ? 1 : 0;
        }
        float out[kChannels] = {0.0f, 0.0f};
        if (m_level > 0) {
            float now[kChannels];
            if (fetch(m_head, now)) {
                out[kLeft] = now[kLeft];
                out[kRight] = now[kRight];
                if (m_fadeLeft > 0) {
                    const float across = 1.0f - static_cast<float>(m_fadeLeft) / static_cast<float>(m_fadeLength);
                    float before[kChannels] = {0.0f, 0.0f};
                    (void)fetch(m_fadingFrom, before);
                    out[kLeft] = before[kLeft] + (now[kLeft] - before[kLeft]) * across;
                    out[kRight] = before[kRight] + (now[kRight] - before[kRight]) * across;
                    ++m_fadingFrom;
                    --m_fadeLeft;
                }
                ++m_head;
            } else {
                // The spill is behind: the head waits for it rather than
                // skipping what it has not got.
                ++m_underruns;
                if (m_spill != nullptr && !m_pending && !m_spill->covers(m_head)) {
                    m_spill->request(m_head);
                }
            }
            if (m_level < m_fadeLength) {
                const float level = static_cast<float>(m_level) / static_cast<float>(m_fadeLength);
                out[kLeft] *= level;
                out[kRight] *= level;
            }
        } else {
            // Silent, nothing is being faded from; and a head further back
            // than memory has the spill read ahead for when it plays again.
            m_fadeLeft = 0;
            if (m_spill != nullptr && !m_pending && isFar(m_head) && !m_spill->covers(m_head)) {
                m_spill->request(m_head);
            }
        }
        if (frame < leftOut.size()) {
            leftOut[frame] = out[kLeft];
        }
        if (frame < rightOut.size()) {
            rightOut[frame] = out[kRight];
        }
    }

    if (m_spill != nullptr) {
        // The earliest the far ring is still wanted from, which frees what
        // is before it for the spill to fill.
        std::uint64_t need = std::numeric_limits<std::uint64_t>::max();
        if (isFar(m_head)) {
            need = m_head;
        }
        if (m_fadeLeft > 0 && isFar(m_fadingFrom)) {
            need = std::min(need, m_fadingFrom);
        }
        if (m_pending && m_written > 0 && isFar(pendingTarget())) {
            need = std::min(need, pendingTarget());
        }
        if (need != std::numeric_limits<std::uint64_t>::max()) {
            m_spill->consumed(need);
        }
    }
    if (m_meter != nullptr) {
        m_meter->publish(kOffsetValue, static_cast<float>(static_cast<double>(offsetSamples()) / m_sampleRate));
        m_meter->publish(kAvailableValue, static_cast<float>(static_cast<double>(availableSamples()) / m_sampleRate));
        m_meter->publish(kPausedValue, paused ? 1.0f : 0.0f);
        m_meter->publish(kUnderrunValue, static_cast<float>(m_underruns));
    }
}

void TimeShift::reset() noexcept RB_NONBLOCKING
{
    // What has been kept is kept: a cut in the signal path is not a reason
    // to forget the last ten minutes.
    m_fadeLeft = 0;
    m_level = m_store.number(Paused) != 0.0f ? 0 : m_fadeLength;
}

void TimeShift::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    // A new Jump count, or a change of Paused, is acted on at the top of
    // the next cycle.
    m_store.applyNumber(index, value);
}

std::expected<void, BlockError> TimeShift::setParam(std::string_view id, const ParamValue &value)
{
    return m_store.set(id, value);
}

ParamValue TimeShift::param(std::string_view id) const
{
    return m_store.value(id);
}

ParamValueMap TimeShift::saveState() const
{
    return m_store.save();
}

std::expected<void, BlockError> TimeShift::loadState(const ParamValueMap &state)
{
    const std::expected<void, BlockError> loaded = m_store.load(state);
    if (loaded && m_prepared) {
        m_seenJump = m_store.number(Jump);
    }
    return loaded;
}

} // namespace rb::blocks

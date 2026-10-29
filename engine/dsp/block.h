// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audioblock.h"
#include "dsp/blockinfo.h"
#include "dsp/channelmap.h"
#include "dsp/paramdescriptor.h"
#include "dsp/paramvalue.h"
#include "rt/meter.h"
#include "rt/realtime.h"

#include <cstdint>
#include <expected>
#include <span>
#include <string_view>

namespace rb::rt
{
class AudioTap;
} // namespace rb::rt

namespace rb::dsp
{

/// What can go wrong when a block is asked to do something. These are values,
/// not exceptions: nothing here throws, and nothing reaches the audio thread.
enum class BlockError : std::uint8_t {
    UnknownType, ///< No block of that type id is registered.
    DuplicateType, ///< That type id is registered already.
    UnknownParam, ///< No parameter of that id.
    ParamTypeMismatch, ///< The value is not of the parameter's type.
    ParamOutOfRange, ///< The value is outside the parameter's range or choices.
    ParamReadOnly, ///< The block writes that parameter; nobody else may.
    InvalidDescriptor, ///< The block's own parameter list does not check out.
    UnsupportedSpec, ///< The block cannot work at that rate or that size.
};

/// The stable name of an error, for diagnostics and for the facade.
[[nodiscard]] std::string_view blockErrorName(BlockError error) noexcept;

/// What a block is being prepared for. Everything that costs memory is
/// decided here, so that a cycle never has to allocate.
struct PrepareSpec {
    double sampleRate = 48000.0;
    /// The longest cycle the block will be given. Never above kMaxBlockFrames.
    std::uint32_t maxBlockFrames = kMaxBlockFrames;
    std::uint32_t inputChannels = 2;
    std::uint32_t outputChannels = 2;
    /// **What the input ports carry**, when the host knows: a source block
    /// capturing an application that is playing to a 5.1 sink is told so
    /// here, because a downmix needs to know which channel is the centre and
    /// which is the low-frequency one, not merely how many there are.
    ///
    /// Empty means nobody has said, and a block should make what it can of
    /// inputChannels. A block that does not mix channels can ignore it.
    ChannelLayout inputLayout;
    /// **Where a second, separate group of input channels starts** inside
    /// inputLayout, for a block that takes audio from two places at once —
    /// an application and the microphone that application is itself
    /// recording from. The two groups have channels of the same name, so
    /// they cannot be told apart by position alone.
    ///
    /// Zero, which is what every other block gets, means there is one group.
    std::uint32_t secondInputFrom = 0;
    /// The same for the output ports: an output block sending the session's
    /// stereo bus to a 5.1 sink is told which channel is which, so that the
    /// sound lands where it belongs instead of on the first two ports.
    ChannelLayout outputLayout;
    /// **Where a block publishes numbers of its own** — a gain reduction, a
    /// level, whatever its face shows — for the telemetry to pick up. Null
    /// when nobody is watching, which a block must cope with.
    ///
    /// The levels either side of a block are measured by the graph and need
    /// nothing from the block; this is for what only the block knows.
    rt::BlockMeter *meter = nullptr;
    /// **Where a recording block leaves what it hears** for a writer off the
    /// audio thread to put in a file. Given only to a block that says it
    /// records (BlockInfo::recordsAudio), and outlives the block: a block
    /// that is replaced by a fresh one for a new rate is handed the same
    /// tap, so the recording carries on across it. Null when nobody is
    /// recording, which a block must cope with.
    rt::AudioTap *recording = nullptr;
};

/// Every lane present: what a block is told when nobody knows of lanes at
/// all, which is every run that is not a session node's.
inline constexpr std::uint32_t kEveryLanePresent = ~0U;

/// One cycle's worth of work.
///
/// \a frames is the length of this cycle, from kMinBlockFrames to the
/// maxBlockFrames the block was prepared for. It is given separately because
/// a source has no input to take it from.
///
/// A channel of \a input with no storage is **silence**, which is how an
/// unconnected port arrives; ask AudioBlock::isSilent() rather than assume.
/// The block writes every frame of every output channel that has storage.
struct ProcessContext {
    AudioBlock input;
    AudioBlock output;
    std::uint32_t frames = 0;
    /// **Which of the session node's input lanes had audio arriving** this
    /// cycle, one bit per lane in the order the node numbers them: a lane is
    /// a capture block's ports, and a port nothing is linked to has no
    /// buffer. What a Recorder reads to tell an application that has stopped
    /// playing from one that is playing silence. Every bit is set
    /// when nobody knows of lanes.
    std::uint32_t sourcePresent = kEveryLanePresent;
};

/// What every block implements: sources, effects, meters and outputs alike.
///
/// The lifetime is: build, prepare(), then any number of cycles of
/// applyParam() and process(), with reset() whenever the signal path is cut.
/// A block **keeps its state across a recompile** of the graph, so nothing
/// here is thrown away when the session is edited.
///
/// The division that matters is prepare() against process(). prepare() may
/// allocate and takes as long as it likes; process() may do neither and runs
/// on the audio thread under the [real-time
/// rules](../../docs/architecture.md#real-time-rules).
class Block
{
public:
    Block() = default;
    virtual ~Block() = default;

    Block(const Block &) = delete;
    Block &operator=(const Block &) = delete;

    /// What this block is. The reference stays valid for the block's life;
    /// its latencySamples may change at prepare().
    [[nodiscard]] virtual const BlockInfo &info() const noexcept = 0;

    /// The block's parameters, in the order the settings panel shows them.
    /// The span stays valid for the block's life.
    [[nodiscard]] virtual std::span<const ParamDescriptor> params() const noexcept = 0;

    /// Gets ready for the given rate and cycle length, allocating whatever it
    /// needs. Not real-time; called before the graph goes live and again when
    /// the rate changes, or what preparedShape() says.
    [[nodiscard]] virtual std::expected<void, BlockError> prepare(const PrepareSpec &spec) = 0;

    /// **What its settings say it has to be prepared for**, beyond the rate
    /// and its channels: a number that stands for a choice prepare() makes
    /// from them, about the memory the block holds and the latency it
    /// reports. Frequency Raising's frame is one. Zero, which never changes,
    /// for nearly every block.
    ///
    /// The runtime asks after every setting it hands the block, and when the
    /// answer is not what the block was last prepared for it prepares a
    /// fresh one with the same settings and works the graph out again around
    /// its latency (ADR-0022). So it must follow from the settings alone, and
    /// a setting that moves it should do so seldom: a move dragged past a
    /// threshold, not every value of a knob. Off the audio thread.
    [[nodiscard]] virtual std::uint32_t preparedShape() const
    {
        return 0;
    }

    /// One cycle. Real-time: no allocation, no locks, no system calls.
    virtual void process(ProcessContext &context) noexcept RB_NONBLOCKING = 0;

    /// Forgets everything the signal left behind — envelopes, filter memory,
    /// delay lines — without changing a parameter. Real-time: it runs between
    /// two cycles when a path is connected or cut.
    virtual void reset() noexcept RB_NONBLOCKING = 0;

    /// Applies a parameter change that arrived through the parameter queue.
    /// \a index is the position in params(). Real-time, so only parameters
    /// whose value is a number travel this way; the rest change the graph and
    /// go through loadState() on a recompile.
    ///
    /// A block smooths the change inside itself; this never steps the signal.
    virtual void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING = 0;

    /// Sets one parameter by id, off the audio thread. This is what the
    /// session model's setParam edit command reaches.
    [[nodiscard]] virtual std::expected<void, BlockError> setParam(std::string_view id, const ParamValue &value) = 0;

    /// The value of one parameter, or an empty value if there is no such
    /// parameter.
    [[nodiscard]] virtual ParamValue param(std::string_view id) const = 0;

    /// Everything that belongs in the session file: every parameter that is
    /// not read-only, by id. Read-only parameters are block outputs and are
    /// published as telemetry, not saved.
    [[nodiscard]] virtual ParamValueMap saveState() const = 0;

    /// Puts a saved state back. A parameter the state does not mention keeps
    /// its default; one the block does not know is ignored, so that a session
    /// written by a later version still opens.
    [[nodiscard]] virtual std::expected<void, BlockError> loadState(const ParamValueMap &state) = 0;

    /// How many samples of delay the block adds **on purpose**, as its job
    /// rather than as a cost of doing it: a Sync block's setting.
    ///
    /// **Not latency.** BlockInfo::latencySamples is a delay the block cannot
    /// help, which latency compensation lines every other path up with and
    /// which a bypassed block keeps so that switching it off shifts nothing.
    /// A delay added on purpose is the opposite on both counts: lining the
    /// other paths up with it would undo it, and switching the block off is
    /// how somebody takes it away. So it is reported here, apart, for what a
    /// path's total delay is to be shown as — never compensated.
    ///
    /// It may change from one cycle to the next without the graph being
    /// worked out again. Real-time: it reads what the block already holds.
    [[nodiscard]] virtual std::uint32_t intendedDelaySamples() const noexcept RB_NONBLOCKING
    {
        return 0;
    }
};

} // namespace rb::dsp

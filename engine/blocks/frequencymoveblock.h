// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/ear.h"
#include "dsp/frequencymove.h"
#include "dsp/paramstore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// A move's four ends as a block sets them before anybody does.
struct MoveRanges {
    float sourceLow;
    float sourceHigh;
    float destinationLow;
    float destinationHigh;
};

/// What makes a frequency-moving block the one it is: which way it carries
/// sound, and what it starts from. Frequency Lowering and Frequency
/// Raising are the same settings round dsp::FrequencyMover, each with
/// one of these.
struct FrequencyMoveKind {
    std::string_view typeId;
    /// The stable ids of its methods, in the order the stored number counts
    /// them — dsp::MoveMethod's order, as far as the block offers them.
    std::span<const std::string_view> methodIds;
    std::size_t defaultMethod = 0;
    /// Which of FrequencyMoveBlock::kSourceIds a move starts with.
    std::size_t defaultSource = 0;
    std::array<MoveRanges, dsp::FrequencyMover::kMaxMoves> defaults{};
    float minimumHz = 125.0f;
    float maximumHz = 16000.0f;
    /// Whether the destination is above the source (a raising) rather than
    /// below it (a lowering). A move set the other way does not act.
    bool raises = false;
    /// The mover's frame, dsp::FrequencyMover::prepare().
    double frameMs = dsp::FrequencyMover::kFrameMs;
    /// **A longer frame for a move that starts low**, or 0 for one frame
    /// whatever the moves: the frame a move whose source reaches below
    /// longFrameBelowHz needs to be told apart. The block takes it only
    /// while such a move is in use, and its latency follows.
    double longFrameMs = 0.0;
    float longFrameBelowHz = 0.0f;
};

/// Moves sound out of ranges one ear cannot use into ranges it hears, for
/// that ear: the settings of dsp::FrequencyMover, which does the moving.
///
/// **One ear.** The block works on the side its Ear setting names and passes
/// the other through, lined up (dsp::EarRouter); the same block is
/// added twice for two ears.
///
/// **Up to kMoveCount moves**, each from a source range into a destination
/// range on the kind's side of it, by a method of the kind's, at a level of
/// its own, keeping, lowering or emptying the source, always or only while
/// the source range holds the sound. Every move reads the input, so their
/// order does not matter.
///
/// **Latency:** the mover's frame, reported; the other ear waits as long.
/// For a kind with a long frame, the frame its lowest move needs: which one
/// is preparedShape(), so the runtime prepares the block again when a move
/// crosses the line, and the latency follows (ADR-0022). Until it is
/// prepared again it keeps the frame it has.
///
/// **What it publishes.** kActingValue is a bit a move for each move acting
/// on the last frame, for the tile and the panel. While its panel is open it
/// also publishes band values: for each move, whether it is acting (0 or 1).
class FrequencyMoveBlock : public dsp::Block
{
public:
    static constexpr std::size_t kMoveCount = dsp::FrequencyMover::kMaxMoves;

    static constexpr float kMinimumLevelDb = -24.0f;
    static constexpr float kMaximumLevelDb = 12.0f;

    /// The stable ids of the choices every kind has, in the order the
    /// stored number counts them: a session file holds them.
    static constexpr std::array<std::string_view, 3> kSourceIds{"keep", "lower", "remove"};
    static constexpr std::array<std::string_view, 2> kWhenIds{"always", "needed"};

    /// What the block publishes (rt::BlockMeter): which moves acted on the
    /// last frame, as bits, move 1 lowest.
    static constexpr std::size_t kActingValue = 0;

    /// The parameters of one move, kMoveParamCount of them for each, after
    /// the block's own.
    enum MoveParam : std::uint32_t {
        Used = 0,
        On,
        Method,
        SourceLow,
        SourceHigh,
        DestinationLow,
        DestinationHigh,
        Level,
        Source,
        When,
        MoveParamCount,
    };

    /// The block's own parameters, before the moves'.
    enum Param : std::uint32_t {
        Ear = 0,
        MovedOnly,
        FirstMoveParam,
    };

    /// The id of \a which parameter of move \a move (0 to kMoveCount - 1):
    /// "move1SourceLow" and so on.
    [[nodiscard]] static std::string paramId(std::size_t move, MoveParam which);
    /// Where that parameter sits in params().
    [[nodiscard]] static constexpr std::uint32_t paramIndex(std::size_t move, MoveParam which) noexcept
    {
        return FirstMoveParam + static_cast<std::uint32_t>(move) * MoveParamCount + which;
    }

    /// \a kind outlives the block: each kind is a constant of its own.
    explicit FrequencyMoveBlock(const FrequencyMoveKind &kind);

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    /// 1 while a move in use starts below the kind's longFrameBelowHz, so it
    /// needs the long frame; otherwise 0.
    [[nodiscard]] std::uint32_t preparedShape() const override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// The frame the mover would be prepared with now, in milliseconds: the
    /// kind's, or its long one while preparedShape() asks for it.
    [[nodiscard]] double frameMs() const;

    /// Move \a move as the settings have it, for the mover and for a test.
    [[nodiscard]] dsp::FrequencyMove moveSettings(std::size_t move) const noexcept RB_NONBLOCKING;

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors(const FrequencyMoveKind &kind);
    /// Hands the mover what the settings say now: every cycle, since a
    /// setting may be changed from the engine thread at any moment and the
    /// store is where both threads meet. It costs four small comparisons.
    void takeSettings() noexcept RB_NONBLOCKING;

    const FrequencyMoveKind &m_kind;
    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;
    dsp::EarRouter m_router;
    dsp::FrequencyMover m_mover;
};

} // namespace rb::blocks

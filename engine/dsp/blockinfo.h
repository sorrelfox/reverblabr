// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rb::dsp
{

/// Where a block appears in the library sidebar.
enum class BlockCategory : std::uint8_t {
    Sources,
    Outputs,
    Effects,
    Meters,
    Advanced,
    Plugins,
    /// Blocks for listening with an ear that hears some sounds only faintly
    /// or not at all, each working on one ear.
    Hearing,
};

/// What the block's tile shows at a glance, which tells the canvas what to
/// draw without knowing anything about the block itself.
enum class FaceKind : std::uint8_t {
    Meter, ///< A level meter, for example the Peak/RMS block.
    Value, ///< One number, for example a gain in decibels.
    Icon, ///< An icon alone, for a block with nothing to show.
    Text, ///< A line of text, for example the captured application's name.
    Bars, ///< Several small bars, for example a band equaliser.
    Curve, ///< A drawn curve, for example a dynamics response.
    Vu, ///< A pair of needles, for the VU meter.
};

/// The stable name of a category or a face, for the facade. Never translated.
[[nodiscard]] std::string_view blockCategoryName(BlockCategory category) noexcept;
[[nodiscard]] std::string_view faceKindName(FaceKind face) noexcept;

/// How many ports of one direction a block has, and what they are called.
///
/// A block with a fixed shape has \a minCount equal to \a maxCount. A block
/// whose shape follows what is connected to it, such as a mixer, has a range,
/// and the graph compiler picks a count inside it.
///
/// The labels are stable ids such as "in" or "sidechain", not channel names
/// and never translated; the interface has its own table.
///
/// **Lanes**. A side may carry more than one signal: a ducker takes
/// the voice and the music it turns down, a mixer several sources. Each
/// signal is a *lane* of \a laneWidth consecutive ports — a stereo pair,
/// one port a channel — and the canvas draws one connector a lane. A width
/// of nought is the usual case: however many ports there are, they are one
/// lane. \a laneLabels names the lanes with stable ids such as "main" and
/// "background", which the interface turns into words.
struct PortSpec {
    std::uint32_t minCount = 0;
    std::uint32_t maxCount = 0;
    std::vector<std::string> labels;
    std::uint32_t laneWidth = 0;
    std::vector<std::string> laneLabels;

    [[nodiscard]] static PortSpec fixed(std::uint32_t count, std::vector<std::string> labels = {})
    {
        return PortSpec{count, count, std::move(labels), 0, {}};
    }

    /// \a lanes lanes of \a width ports each, with their labels.
    [[nodiscard]] static PortSpec
    lanes(std::uint32_t width, std::vector<std::string> laneLabels, std::vector<std::string> labels = {})
    {
        const auto count = static_cast<std::uint32_t>(laneLabels.size()) * width;
        return PortSpec{count, count, std::move(labels), width, std::move(laneLabels)};
    }

    /// Which lane port \a index belongs to: always the first when the side
    /// is one lane.
    [[nodiscard]] std::uint32_t laneOf(std::uint32_t index) const noexcept
    {
        return laneWidth == 0 ? 0 : index / laneWidth;
    }

    /// The label of port \a index, or an empty view if it has none.
    [[nodiscard]] std::string_view label(std::uint32_t index) const noexcept
    {
        return index < labels.size() ? std::string_view{labels[index]} : std::string_view{};
    }
};

/// What a block is, as opposed to what it is doing. Everything here is stable
/// across versions except the latency, which a block may work out in
/// prepare() once it knows the sample rate.
///
/// There is no name and no description: those are translated and belong to
/// the interface, which looks them up by \a typeId (see docs/i18n.md).
struct BlockInfo {
    /// Stable for ever, of the form "<category>.<name>", for example
    /// "effect.volume". A session file holds this.
    std::string typeId;
    BlockCategory category = BlockCategory::Effects;
    PortSpec inputs;
    PortSpec outputs;
    /// How many samples the block delays the signal by. Latency compensation
    /// lines the paths up with it. A block that knows its latency only after
    /// prepare() sets it there. A delay the block adds on purpose is not
    /// latency and is not here; see Block::intendedDelaySamples().
    std::uint32_t latencySamples = 0;
    FaceKind face = FaceKind::Icon;

    /// Whether the audio system fills these ports, rather than wires from
    /// other blocks.
    ///
    /// **A capture block's inputs are not wiring.** What arrives at one is
    /// the application or the device it follows, put there by the audio
    /// system when the session starts; a wire into the same ports is two
    /// things writing one signal, and neither of them knows about the
    /// other. The same the other way round for an output block's outputs,
    /// which belong to the device.
    ///
    /// **Said here rather than read off the category**. The category
    /// is where a block appears in the library sidebar, and a later block
    /// that sits under Sources without being fed by the audio system — or
    /// an Advanced one that is — would make the sidebar decide what the
    /// graph does. The compiler, the session's edit rules and the canvas
    /// all ask this instead, so there is one answer and the block gives it.
    bool inputsFromAudioSystem = false;
    bool outputsToAudioSystem = false;

    /// Whether the block hands what it hears to a writer off the audio
    /// thread, through PrepareSpec::recording. The runtime makes a tap only
    /// for a block that says so, because a tap is ten seconds of audio held
    /// in memory.
    bool recordsAudio = false;
};

} // namespace rb::dsp

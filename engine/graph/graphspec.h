// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/audioblock.h"
#include "dsp/block.h"
#include "rt/meter.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rb::graph
{

/// One block in a graph that is about to be compiled.
///
/// The block itself belongs to the session runtime, not to the graph: a
/// recompile builds a new plan around the same blocks, which is what lets an
/// envelope or a filter's memory survive an edit. The graph never deletes a
/// block and never touches one after it has been retired.
struct NodeSpec {
    /// The session's block id, for example "b1". Carried through so that the
    /// runtime can match a node to a PipeWire port; the graph does not read it.
    std::string id;
    /// Not owned, and must outlive the compiled graph. Never null.
    dsp::Block *block = nullptr;
    /// A disabled block is compiled to a pass-through rather than left out,
    /// so that the ports either side of it keep their places. It still
    /// **holds the signal back by as long as the block itself would have**,
    /// so switching a block off does not shift it against a parallel path.
    bool enabled = true;
    /// How long to cross-fade when this block has just been switched on or
    /// off, in samples. Zero means the change takes effect at the next
    /// sample, which is what a graph built from scratch wants; a session
    /// being edited asks for about 10 ms.
    ///
    /// The fade runs between what the block makes of the signal and the
    /// signal itself, so the block goes on running for the length of it.
    std::uint32_t crossfadeSamples = 0;
    /// Where this block's levels are accumulated, or null for a block nobody
    /// is metering. Not owned, and must outlive the compiled graph.
    ///
    /// **The graph is where a meter has to be read**, not the node that
    /// publishes the ports: a buffer is reused as soon as nothing still has
    /// to read it, so by the end of a cycle an intermediate block's output
    /// may hold something another block wrote. Measured here, each block's
    /// output is measured while it is still the block's own.
    rt::BlockMeter *meter = nullptr;
};

/// One wire, by node index into GraphSpec::nodes.
struct EdgeSpec {
    std::size_t fromNode = 0;
    std::uint32_t fromPort = 0;
    std::size_t toNode = 0;
    std::uint32_t toPort = 0;

    [[nodiscard]] bool operator==(const EdgeSpec &other) const = default;
};

/// What to compile: the blocks, the wires between them, and the shape of the
/// cycles they will run in.
///
/// This is deliberately plain C++ with no session, no JSON and no Qt: the
/// graph layer has to be testable offline, so the layer above converts.
struct GraphSpec {
    std::vector<NodeSpec> nodes;
    std::vector<EdgeSpec> edges;
    double sampleRate = 48000.0;
    /// Buffers are made for this many frames, so that a change of PipeWire's
    /// quantum needs no recompile. Never above dsp::kMaxBlockFrames.
    std::uint32_t maxBlockFrames = dsp::kMaxBlockFrames;
};

} // namespace rb::graph

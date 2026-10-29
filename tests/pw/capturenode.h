// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

// A capture node for integration tests: a PipeWire filter with one input port
// per channel and a peak meter on each port. The policy never links it; tests
// create its links themselves, as the engine will. It uses libpipewire
// directly and shares no code with the engine, so it can referee the engine.

#include "pwtest.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rb::pwtest
{

struct CaptureNodeOptions {
    std::string name = "rbtest.capture"; ///< node.name
    std::vector<std::string> channels{"FL", "FR"}; ///< one input port per channel, named input_<channel>
    std::string mediaClass; ///< media.class; empty leaves it unset
    std::map<std::string, std::string> props; ///< further node properties
    /// Frequency of the sine the ports are expected to carry, for
    /// measureSine(), at the harness's graph rate of 48 kHz; 0 turns the
    /// check off.
    double sineHz = 0.0;
    /// A cycle whose sine residual rises above this, in dBFS, counts as a
    /// click in SineCheck::clickedCycles.
    double clickResidualDb = -55.0;
};

/// What measureSine() found on one port.
struct SineCheck {
    double peakDb = -200.0;
    /// Largest deviation from a clean sine, in dBFS: the residual of
    /// x[n] - 2cos(w)x[n-1] + x[n-2], which is zero for a pure sine. A click,
    /// a gap or a repeated block shows up as a large residual.
    double residualDb = -200.0;
    std::uint64_t samples = 0;
    std::uint64_t nullBuffers = 0; ///< cycles where the port had no buffer
    /// Cycles the capture node itself lost (its graph position jumped). Such
    /// a gap clicks by itself, so a window with any is not evidence.
    std::uint64_t lostCycles = 0;
    /// Cycles the port ran with a buffer, and how many of them clicked
    /// (their residual rose above CaptureNodeOptions::clickResidualDb). A
    /// machine too busy to feed the graph in time clicks now and then; a
    /// session that is broken clicks in a steady share of every cycle, so
    /// the count tells the two apart where the loudest click cannot.
    std::uint64_t cycles = 0;
    std::uint64_t clickedCycles = 0;
};

class CaptureNode
{
public:
    explicit CaptureNode(CaptureNodeOptions options = {});
    ~CaptureNode();
    CaptureNode(const CaptureNode &) = delete;
    CaptureNode &operator=(const CaptureNode &) = delete;

    /// The node as the graph shows it once all its ports exist; nothing if it didn't appear.
    [[nodiscard]] std::optional<Graph::Node> waitUntilReady(std::chrono::milliseconds timeout = kDefaultTimeout) const;

    /// Peak level per channel, in dBFS, over the next duration of processing.
    /// -200 means silence; a channel whose port never ran is missing.
    [[nodiscard]] std::map<std::string, double> measure(std::chrono::milliseconds duration = 300ms);

    /// Like measure(), but repeats until two measurements in a row agree, so a
    /// volume change still on its way doesn't skew the result.
    [[nodiscard]] std::map<std::string, double> measureSettled(std::chrono::milliseconds timeout = kDefaultTimeout);

    /// Peak and sine residual per channel over the next duration. Needs
    /// CaptureNodeOptions::sineHz.
    [[nodiscard]] std::map<std::string, SineCheck> measureSine(std::chrono::milliseconds duration);

    /// The amplitude of one frequency on each port, in dBFS, over the next
    /// duration. **Measures that frequency alone**, so two applications
    /// playing different tones into the same capture can each be read off
    /// without the other getting in the way. -200 means nothing there.
    [[nodiscard]] std::map<std::string, double> measureTone(double hz, std::chrono::milliseconds duration = 300ms);

    /// Like measureTone(), but repeats until two windows in a row agree to
    /// within a tenth of a decibel on every port, or \a timeout runs out.
    /// **What a level is once it has settled**: a window that began
    /// before a stream reached the capture, or that a loaded machine cut
    /// into, reads low, and the next one does not agree with it.
    [[nodiscard]] std::map<std::string, double> measureToneSettled(double hz,
                                                                   std::chrono::milliseconds duration = 300ms,
                                                                   std::chrono::milliseconds timeout = kDefaultTimeout);

    /// Number of process cycles run so far.
    [[nodiscard]] std::uint64_t cycles() const;

    [[nodiscard]] const CaptureNodeOptions &options() const;

    struct Impl;

private:
    CaptureNodeOptions m_options;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rb::pwtest

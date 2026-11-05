// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/biquad.h"
#include "dsp/block.h"
#include "dsp/paramstore.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Compares the two channels of a lane and publishes which side is louder,
/// how loud, and whether the two sides carry one sound.
///
/// **It reads the lane and changes nothing:** the sound comes out sample for
/// sample with no delay. Only numbers cross to the window process, which keeps
/// the word, the hold and the trail in one place (app/soundsidereadings.h).
///
/// **A sum of energies over a short window**, moved along in 5 ms hops. The
/// third sum, of the two sides multiplied, tells one sound straight ahead from
/// a wide bed: both have near-equal sides, and only the first has sides that
/// match.
///
/// **The loudest window is held until somebody could have read it.** A block
/// publishes once a cycle and a reader comes every 16 ms, so a later store
/// overwrites an earlier one. Keeping the loudest for a fixed 20 ms is not
/// enough: at a large quantum many windows go by inside one process(), and the
/// last may be the quietest. So a window stands for 20 ms and has been
/// published before anything but a louder one replaces it. How long a hold
/// lasts therefore depends on the quantum; the pan of a steady sound does not.
///
/// **Silence is not the middle.** Under the gate the state is Quiet, and a
/// surface shows no mark at all.
///
/// **A block can't write back to the session,** so "To stays an octave above
/// From" is computed here for what the block uses, whichever of the two moved
/// last. The card writes both values with the same rule (SoundSideWords.qml),
/// so that the session holds what is shown.
class SoundSide final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "meter.soundside";

    /// What the block publishes (rt::BlockMeter).
    static constexpr std::size_t kPanValue = 0; ///< -1 left to +1 right.
    static constexpr std::size_t kLevelValue = 1; ///< dBFS.
    static constexpr std::size_t kWidthValue = 2; ///< 1 one sound, 0 unrelated.
    static constexpr std::size_t kStateValue = 3; ///< A State.

    /// Published as a number, so the values are the window's too.
    enum State : std::uint32_t {
        Quiet = 0, ///< Under the gate.
        Sounding,
        SidesAlike, ///< The two sides have matched for kAlikeMs.
    };

    static constexpr std::uint32_t kHopMs = 5;
    static constexpr std::uint32_t kWindowHops = 4;
    /// How long the loudest window stands, and it is published first.
    static constexpr std::uint32_t kHoldMs = 20;
    static constexpr std::uint32_t kAlikeMs = 5000;
    static constexpr float kAlikePan = 0.001f;
    static constexpr float kAlikeWidth = 0.999f;
    /// What no sound at all reads as: the sum's own floor.
    static constexpr float kNoLevel = -120.0f;

    static constexpr float kLowestFrom = 100.0f;
    static constexpr float kHighestFrom = 8000.0f;
    static constexpr float kLowestTo = 500.0f;
    static constexpr float kHighestTo = 16000.0f;
    /// To stays at least this many times From.
    static constexpr float kLeastBandRatio = 2.0f;

    enum Param : std::uint32_t {
        GateDb = 0, ///< dBFS; under it there is no side to show.
        Band, ///< all, range.
        FromHz, ///< On Range: the side path's high-pass.
        ToHz, ///< On Range: the side path's low-pass.
        ShowStrip, ///< Whether the strip on screen shows this block.
    };

    /// From and To after one moved, the one that moved standing.
    struct BandEdges {
        float from = 1000.0f;
        float to = 6000.0f;
    };
    [[nodiscard]] static BandEdges octaveApart(float from, float to, bool toMoved) noexcept RB_NONBLOCKING;

    SoundSide();

    [[nodiscard]] static std::unique_ptr<dsp::Block> make();

    [[nodiscard]] const dsp::BlockInfo &info() const noexcept override
    {
        return m_info;
    }

    [[nodiscard]] std::span<const dsp::ParamDescriptor> params() const noexcept override
    {
        return m_descriptors;
    }

    [[nodiscard]] std::expected<void, dsp::BlockError> prepare(const dsp::PrepareSpec &spec) override;
    void process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING override;
    void reset() noexcept RB_NONBLOCKING override;
    void switchedOn() noexcept RB_NONBLOCKING override;
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    [[nodiscard]] float pan() const noexcept RB_NONBLOCKING
    {
        return m_pan;
    }

    [[nodiscard]] float level() const noexcept RB_NONBLOCKING
    {
        return m_state == Quiet ? m_level : m_heldLevel;
    }

    [[nodiscard]] float width() const noexcept RB_NONBLOCKING
    {
        return m_width;
    }

    [[nodiscard]] State state() const noexcept RB_NONBLOCKING
    {
        return m_state;
    }

    /// The edges the side path goes by.
    [[nodiscard]] BandEdges bandInUse() const noexcept RB_NONBLOCKING;

private:
    struct Energies {
        double left = 0.0;
        double right = 0.0;
        double both = 0.0;
    };

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    void followBand() noexcept RB_NONBLOCKING;
    void closeHop() noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    /// Which edge an edit moved last. Written by whichever thread brought the
    /// edit: the engine's for setParam(), the audio thread's for the queue.
    std::atomic<bool> m_toMovedLast{false};

    double m_sampleRate = 48000.0;
    std::uint32_t m_hopFrames = 240;

    bool m_bandOn = false;
    BandEdges m_edges{0.0f, 0.0f};
    dsp::SvfCoefficients m_highPass;
    dsp::SvfCoefficients m_lowPass;
    std::array<dsp::Svf, 2> m_highPassSide;
    std::array<dsp::Svf, 2> m_lowPassSide;

    Energies m_hop;
    std::uint32_t m_hopFilled = 0;
    std::array<Energies, kWindowHops> m_ring{};
    std::uint32_t m_ringNext = 0;

    double m_heldEnergy = 0.0;
    std::uint32_t m_heldAgeMs = kHoldMs;
    bool m_heldPublished = true;
    std::uint32_t m_alikeMs = 0;

    float m_pan = 0.0f;
    float m_width = 0.0f;
    float m_level = kNoLevel;
    float m_heldLevel = kNoLevel;
    State m_state = Quiet;
    rt::BlockMeter *m_meter = nullptr;
};

} // namespace rb::blocks

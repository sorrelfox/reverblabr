// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/envelope.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Turns a background source down while a main one — a voice, usually — is
/// heard, and brings it back up when the main one stops.
///
/// **Two lanes in, one out** (dsp::PortSpec::lanes): the main signal on the
/// first pair of ports, the background on the second. What comes out is the
/// two of them mixed, the background at whatever it has been turned down
/// to. The main signal is never touched.
///
/// **A ramp, not a jump.** The background's gain moves in a straight line:
/// the attack is how long it takes to go all the way from full to the ducked
/// volume, the release how long to come all the way back. So the background
/// is at the ducked volume within the attack of the voice starting, and back
/// at full within the hold and the release of it stopping, whatever the
/// ducked volume is set to.
///
/// **The hold bridges the gaps in speech.** A voice falls below any
/// threshold between words; without the hold the music would come up in
/// every one of those gaps and go down again, which is the pumping a ducker
/// exists not to make.
class Ducking final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.ducking";

    /// Which of the block's published values is which (rt::BlockMeter).
    /// **How far down the background is**, in decibels (nought when it is
    /// not ducked), and whether the block is ducking — the main signal
    /// heard, or the hold still running after it — as 1 or 0. What the face
    /// and the panel's status line read.
    static constexpr std::size_t kDuckingValue = 0;
    static constexpr std::size_t kActiveValue = 1;

    /// The main signal's detector lets go this quickly. **Short**, because
    /// the hold is what bridges the gaps between words; a slow detector
    /// would add its own tail to the hold and the release, and the times on
    /// the panel would stop being the times it takes.
    static constexpr float kDetectorReleaseMs = 10.0f;

    enum Param : std::uint32_t {
        Threshold = 0,
        DuckedVolume,
        Attack,
        Hold,
        Release,
    };

    Ducking();

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
    void applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING override;

    [[nodiscard]] std::expected<void, dsp::BlockError> setParam(std::string_view id,
                                                                const dsp::ParamValue &value) override;
    [[nodiscard]] dsp::ParamValue param(std::string_view id) const override;
    [[nodiscard]] dsp::ParamValueMap saveState() const override;
    [[nodiscard]] std::expected<void, dsp::BlockError> loadState(const dsp::ParamValueMap &state) override;

    /// The gain on the background as it stands, from nought to one.
    [[nodiscard]] float backgroundGain() const noexcept RB_NONBLOCKING
    {
        return m_gain;
    }

    /// Whether the main signal was above the threshold at the end of the
    /// last cycle.
    [[nodiscard]] bool mainPresent() const noexcept RB_NONBLOCKING
    {
        return m_present;
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;

    double m_sampleRate = 48000.0;
    dsp::PeakFollower m_detector;
    /// The background's gain now, and how many samples the duck is still
    /// held for after the main signal fell below the threshold.
    float m_gain = 1.0f;
    std::uint32_t m_hold = 0;
    bool m_present = false;
};

} // namespace rb::blocks

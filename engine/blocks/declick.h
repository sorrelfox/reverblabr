// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/autoregressive.h"
#include "dsp/block.h"
#include "dsp/paramstore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Takes clicks and pops out: the crackle of a record, a dropout in a
/// digital stream.
///
/// **Found by what cannot be predicted.** Every frame of about 20 ms
/// (kFrameMs, rounded up to a power of two, half overlapping the last) gets
/// an autoregressive model of order kOrder (dsp::Autoregressive). Music and
/// speech are mostly predictable from the samples before; a click is not,
/// so the prediction error jumps where one is. The error is filtered once
/// more by the model backwards (a matched filter for an impulse through the
/// model), which gathers a click's error back onto the samples it hit, and
/// a sample is a click's when that is more than "threshold" above what the
/// frame's error usually is: its median, which a few clicks do not move.
///
/// A dropout — the sound gone to exact silence for a few samples, between
/// sound — is found as that: its middle is as predictable as silence, so
/// the error alone only finds its ends.
///
/// **Mended from what is around it.** What stands out within kJoin samples
/// of what already does is one click, up to kMaxClickMs of it, and it is
/// replaced, with a sample either side, by the samples that best fit a
/// model of the frame with the click weighted out, given everything on
/// both sides: least-squares AR interpolation.
///
/// **Latency.** A frame's middle half is what is mended and passed on, so
/// that a click near its edge still has a model's worth of sound on both
/// sides: three quarters of a frame, reported.
///
/// Each channel is found and mended on its own. The block publishes how
/// many clicks it has mended.
class Declick final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.declick";

    static constexpr float kMinimumThreshold = 0.0f;
    static constexpr float kMaximumThreshold = 20.0f;
    static constexpr float kDefaultThreshold = 3.0f;
    /// How far above the frame's usual error the lowest threshold is. At
    /// the default, clean music and speech are left alone; at the slider's
    /// 0 dB the faintest clicks are found and a note's onset now and then is
    /// taken for one, and changed by little.
    static constexpr float kThresholdFloorDb = 12.0f;

    static constexpr std::size_t kOrder = 32;
    static constexpr double kFrameMs = 20.0;
    static constexpr double kMaxClickMs = 3.0;

    /// What the block publishes (rt::BlockMeter): how many clicks it has
    /// mended since it was prepared.
    static constexpr std::size_t kClicksValue = 0;

    enum Param : std::uint32_t {
        Threshold = 0,
    };

    Declick();

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

    /// Samples per frame at the rate it was prepared for.
    [[nodiscard]] std::size_t frameSize() const noexcept RB_NONBLOCKING
    {
        return m_frameSize;
    }
    /// The longest run of samples mended as one click.
    [[nodiscard]] std::size_t longestClick() const noexcept RB_NONBLOCKING
    {
        return m_longestClick;
    }
    /// Clicks mended since prepare(), on both channels.
    [[nodiscard]] std::uint64_t clicks() const noexcept RB_NONBLOCKING
    {
        return m_clicks;
    }

private:
    static constexpr std::uint32_t kChannels = 2;
    /// Mended either side of what was found: the edges of a click are
    /// quieter than its middle and can fall under the threshold.
    static constexpr std::size_t kMargin = 2;
    /// The least the usual error is taken to be, against the frame's level.
    static constexpr double kLevelFloor = 1.0e-3;
    /// What stands out within this many samples of a click is the same
    /// click.
    static constexpr std::size_t kJoin = 32;
    /// A dropout: at least kShortestDropout samples no louder than
    /// kSilence, with sound louder than kAroundDropout (-80 dBFS) on both
    /// sides.
    static constexpr float kSilence = 1.0e-7f;
    static constexpr std::size_t kShortestDropout = 3;
    static constexpr double kAroundDropout = 1.0e-4;

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    /// Finds and mends the clicks in the middle half of \a channel's frame,
    /// and copies that half to its output.
    void mend(std::uint32_t channel) noexcept RB_NONBLOCKING;
    /// The prediction error and the filtered error of \a frame, again,
    /// for [\a from, \a to).
    void errors(std::span<const float> frame, std::size_t from, std::size_t to) noexcept RB_NONBLOCKING;
    /// Measures and mends the click found at \a seed, first found to run
    /// to \a seedEnd; returns where the mend ends.
    std::size_t measure(std::vector<float> &frame, std::size_t seed, std::size_t seedEnd) noexcept RB_NONBLOCKING;
    /// Whether sample \a n of \a frame is a click's, and where what was
    /// found there ends.
    [[nodiscard]] bool
    found(std::span<const float> frame, std::size_t n, std::size_t &end) const noexcept RB_NONBLOCKING;
    /// How long a dropout starting at \a n is; 0 if there is none.
    [[nodiscard]] std::size_t silentRun(std::span<const float> frame, std::size_t n) const noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    rt::BlockMeter *m_meter = nullptr;

    std::size_t m_frameSize = 0;
    std::size_t m_hop = 0;
    std::size_t m_longestClick = 0;
    /// Each channel's last frame (the newest hop at its end), and the
    /// mended half being played out.
    std::array<std::vector<float>, kChannels> m_frames;
    std::array<std::vector<float>, kChannels> m_out;
    std::size_t m_position = 0;
    std::vector<float> m_window;
    /// Work space: the prediction error, the same filtered back, and a copy
    /// of its size to take the median of.
    std::vector<double> m_error;
    std::vector<double> m_matched;
    std::vector<double> m_sorted;
    double m_limit = 0.0;
    dsp::Autoregressive m_model;
    /// The model a mend is made from, fitted with the click weighted out
    /// (m_fillWindow).
    dsp::Autoregressive m_fill;
    std::vector<float> m_fillWindow;
    std::uint64_t m_clicks = 0;
};

} // namespace rb::blocks

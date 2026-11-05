// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/paramstore.h"
#include "dsp/smoothing.h"
#include "dsp/stereogains.h"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Up to five sources mixed into one, each on a strip of its own: a volume,
/// a mute, a solo and a balance, then a master volume over the lot.
///
/// **Five lanes in, one out** (dsp::PortSpec::lanes), a stereo pair each. A
/// lane nothing is wired to is silence and costs a strip nothing.
///
/// **A volume change takes the time it was asked to take.** A strip's gain
/// moves in a straight line to a new volume over the strip's *ramp*: 20 ms
/// for a slider being dragged, half a second or two for a fade, a second
/// for a scene being recalled. The ramp is a parameter of its own, and the
/// interface sends it in the same edit as the volume it is for — the ramp,
/// then the volume, then the ramp back to 20 ms — so the order the queue
/// delivers them in is the order they happen in. **Which is why a volume
/// change is started in applyParam(), as it arrives, and not read from the
/// store at the top of a cycle** as the other blocks read theirs: by the
/// top of the next cycle all three have arrived, and a volume read then
/// would find the ramp already put back to 20 ms.
///
/// **Scenes** are five saved sets of the five volumes, held in hidden
/// parameters so they are saved with the session and with a preset. The
/// block does nothing with them; recalling one is the interface sending
/// those volumes over the recall time.
class Mixer final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "advanced.mixer";

    static constexpr std::uint32_t kStrips = 5;
    static constexpr std::uint32_t kLaneWidth = 2;
    static constexpr std::size_t kScenes = 5;

    /// How long a volume change takes unless it is told otherwise, and the
    /// times the interface asks for.
    static constexpr float kDefaultRampMs = 20.0f;
    static constexpr float kFadeMs = 500.0f;
    static constexpr float kSlowFadeMs = 2000.0f;
    static constexpr float kSceneRecallMs = 1000.0f;

    /// The mute, the solo, the balance and the master glide over this.
    static constexpr float kSmoothingMs = 20.0f;

    /// Where each strip's parameters sit: Volume + strip * kPerStrip, and so
    /// on, then the master, then the scenes.
    enum StripParam : std::uint32_t {
        Volume = 0,
        Mute,
        Solo,
        Balance,
        Ramp,
        kPerStrip,
    };
    static constexpr std::uint32_t kMaster = kStrips * kPerStrip;
    /// Scene s's volume for strip i is kFirstScene + s * (kStrips + 1) + i;
    /// the one after strip five says whether the scene has been saved.
    static constexpr std::uint32_t kFirstScene = kMaster + 1;

    [[nodiscard]] static constexpr std::uint32_t indexOf(std::uint32_t strip, StripParam which) noexcept
    {
        return strip * kPerStrip + which;
    }

    Mixer();

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

    /// Strip \a strip's volume as it is being applied now, before its mute,
    /// solo and balance.
    [[nodiscard]] float stripVolume(std::uint32_t strip) const noexcept RB_NONBLOCKING
    {
        return strip < kStrips ? m_volumes[strip].current : 0.0f;
    }

private:
    /// A straight line from where the gain is to a new value, over as many
    /// samples as it was asked to take — however far that is.
    struct VolumeRamp {
        float current = 1.0f;
        float target = 1.0f;
        float step = 0.0f;
        std::uint32_t remaining = 0;

        void jump(float value) noexcept RB_NONBLOCKING
        {
            current = value;
            target = value;
            step = 0.0f;
            remaining = 0;
        }

        void start(float value, std::uint32_t samples) noexcept RB_NONBLOCKING
        {
            target = value;
            if (samples == 0) {
                jump(value);
                return;
            }
            remaining = samples;
            step = (target - current) / static_cast<float>(samples);
        }

        [[nodiscard]] float next() noexcept RB_NONBLOCKING
        {
            if (remaining == 0) {
                return current;
            }
            --remaining;
            // Measured back from the target, so it lands on it exactly.
            current = target - step * static_cast<float>(remaining);
            return current;
        }
    };

    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();
    [[nodiscard]] std::uint32_t samplesOf(float ms) const noexcept RB_NONBLOCKING;

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;

    double m_sampleRate = 48000.0;
    std::array<VolumeRamp, kStrips> m_volumes{};
    /// Mute and solo together: one when the strip is heard, nought when not.
    std::array<dsp::LinearSmoother, kStrips> m_gates{};
    std::array<dsp::StereoGainSmoother, kStrips> m_balances{};
    dsp::LinearSmoother m_master;
};

} // namespace rb::blocks

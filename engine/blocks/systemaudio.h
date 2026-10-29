// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "dsp/channelmap.h"
#include "dsp/paramstore.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace rb::blocks
{

/// Everything the machine is playing, in one block.
///
/// **Stream by stream, not from a monitor.** A sink's monitor carries what
/// reaches that sink and nothing can be taken out of it; capturing each
/// playback stream instead is what makes "everything except my chat client"
/// something a person can ask for, and it is what lets alert sounds be left
/// out ([ADR-0007]). PipeWire sums the streams at our ports, which is
/// exactly right: they are different sounds going to one place.
///
/// **What it leaves out, and why.**
///
/// - Alert sounds, unless they are asked for. Nobody means to record the
///   notification that arrived while they were recording.
/// - The applications in its exclusion list, by the same match rule the
///   Application source uses, so a picker can write one and either block can
///   read it.
/// - Every node of ours, from every session, or a recording would be made of
///   itself.
/// - Re-played streams — the output of a loopback, a filter chain, a
///   combined sink or an echo canceller — which would otherwise count the
///   same audio a second time.
class SystemAudio final : public dsp::Block
{
public:
    static constexpr std::string_view kTypeId = "source.system";

    /// How many streams are being captured (rt::BlockMeter). The subtitle —
    /// "All system audio", "All except 2 apps" — is the interface's.
    static constexpr std::size_t kStreamCountValue = 0;

    enum Param : std::uint32_t {
        IncludeAlerts = 0, ///< Whether notification sounds are taken too.
        /// The applications to leave out, by match rule (`binary:firefox`).
        ExcludedApplications,
        IncludeLowFrequency, ///< Whether a 5.1 stream's LFE is folded in.
    };

    SystemAudio();

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

    [[nodiscard]] const dsp::ChannelLayout &inputLayout() const noexcept
    {
        return m_layout;
    }

private:
    [[nodiscard]] static std::vector<dsp::ParamDescriptor> makeDescriptors();

    dsp::BlockInfo m_info;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    dsp::ParamStore m_store;
    dsp::ChannelLayout m_layout;
    std::optional<dsp::ChannelMapper> m_mapper;
};

} // namespace rb::blocks

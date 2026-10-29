// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "plugin/formats.h"
#include "plugin/ladspaabi.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rb::pluginhost
{

/// **A LADSPA library, loaded**, for as long as something holds it:
/// the plugins in it are only good while it is.
class LadspaLibrary
{
public:
    /// The library at \a path, or nothing with the reason in \a error.
    [[nodiscard]] static std::shared_ptr<LadspaLibrary> open(const std::filesystem::path &path, std::string &error);

    ~LadspaLibrary();
    LadspaLibrary(const LadspaLibrary &) = delete;
    LadspaLibrary &operator=(const LadspaLibrary &) = delete;

    /// Every plugin in it, in the library's order.
    [[nodiscard]] const std::vector<const ladspa::Descriptor *> &plugins() const noexcept
    {
        return m_plugins;
    }
    /// The plugin labelled \a label, or null.
    [[nodiscard]] const ladspa::Descriptor *plugin(std::string_view label) const;

private:
    LadspaLibrary() = default;

    void *m_handle = nullptr;
    std::vector<const ladspa::Descriptor *> m_plugins;
};

/// **What the engine calls a LADSPA plugin**: the library's file name and
/// the plugin's label, `amp.so:amp_stereo`. The file name and not the whole
/// path, so that a session made where plugins live in /usr/lib64/ladspa
/// finds the same plugin where they live in /usr/lib/ladspa; a whole path
/// is taken as it is. The library ends at the first `.so:`, since a label
/// may have colons of its own (LSP's are URLs).
[[nodiscard]] std::string ladspaId(const std::filesystem::path &library, std::string_view label);

/// The library and label \a id names, the library found where the LADSPA
/// format looks, or nothing.
struct LadspaLocation {
    std::filesystem::path library;
    std::string label;
};
[[nodiscard]] std::optional<LadspaLocation> locateLadspa(std::string_view id);

/// The rate a sample-rate-relative range is described at. A LADSPA plugin
/// says some bounds as fractions of the rate it runs at, which a scan does
/// not know; the description, and so the panel, shows them at this one.
inline constexpr double kLadspaDescribedRate = 48000.0;

/// **A LADSPA plugin's settings: its control inputs, in port order**, each
/// named by the port's name made into an id (`Gain (dB)` is `gain_db`),
/// with the range and the default its hints give. The scan describes a
/// plugin with these and the host numbers the ring's changes by them.
[[nodiscard]] std::vector<plugin::PluginParam> ladspaParams(const ladspa::Descriptor &descriptor);

/// The description a scan gives of \a descriptor in \a library.
[[nodiscard]] plugin::PluginDescription describeLadspa(const std::filesystem::path &library,
                                                       const ladspa::Descriptor &descriptor);

/// **A LADSPA plugin, run by the host as a block**.
///
/// Audio and control ports; LADSPA has no windows, so the panel is the
/// generic one. The control inputs are the block's parameters in port
/// order, as ladspaParams() describes them. A control output called
/// "latency" is where the plugin says how late it is, by the format's
/// convention.
///
/// **A mono plugin on a stereo lane** runs twice, once a side, or once on
/// the two sides mixed, as an LV2 plugin does; a mono output is sent to
/// both sides.
///
/// **Its state is its control values**, which is all a LADSPA plugin has.
class LadspaPlugin final : public dsp::Block
{
public:
    enum class Mono : std::uint8_t {
        DualMono,
        Downmix,
    };

    /// The plugin \a id names, or nothing when this machine has not got it.
    [[nodiscard]] static std::unique_ptr<LadspaPlugin> make(std::string_view id, Mono mono);
    /// \a descriptor, from \a library.
    [[nodiscard]] static std::unique_ptr<LadspaPlugin>
    make(std::shared_ptr<LadspaLibrary> library, const ladspa::Descriptor *descriptor, Mono mono);

    ~LadspaPlugin() override;

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

    /// Takes the latency the plugin last reported onto info(). The host's
    /// main thread, which is the only one that reads info().
    void refreshLatency() noexcept;

    /// How many instances run: two for a mono plugin run once a side.
    [[nodiscard]] std::size_t instanceCount() const noexcept
    {
        return m_instances.size();
    }

private:
    LadspaPlugin(std::shared_ptr<LadspaLibrary> library, const ladspa::Descriptor *descriptor, Mono mono);

    struct Port {
        enum class Kind : std::uint8_t {
            AudioIn,
            AudioOut,
            ControlIn,
            ControlOut,
        };
        Kind kind = Kind::ControlOut;
        std::uint32_t index = 0;
        /// For a control input: which parameter it is.
        std::uint32_t param = 0;
    };

    void freeInstances() noexcept;

    std::shared_ptr<LadspaLibrary> m_library;
    const ladspa::Descriptor *m_descriptor = nullptr;
    Mono m_mono = Mono::DualMono;
    bool m_inPlaceBroken = false;
    dsp::BlockInfo m_info;
    std::vector<plugin::PluginParam> m_params;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    std::vector<Port> m_ports;
    std::uint32_t m_audioIns = 0;
    std::uint32_t m_audioOuts = 0;
    std::uint32_t m_latencyPort = UINT32_MAX;

    std::vector<ladspa::Handle> m_instances;
    /// Per instance, a value for every port index: controls in, and what
    /// the plugin writes to its control outputs.
    std::vector<std::vector<float>> m_controls;
    /// What the controls are, where the main thread can read them.
    std::vector<std::atomic<float>> m_values;
    std::vector<float> m_silence;
    std::vector<float> m_mix;
    std::vector<float> m_spare;
    /// Per input, a copy to run from when the plugin cannot write over
    /// what it reads.
    std::vector<std::vector<float>> m_inputCopies;
    std::atomic<std::uint32_t> m_latency{0};
};

} // namespace rb::pluginhost

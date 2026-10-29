// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "plugin/formats.h"

#include <lilv/lilv.h>
#include <lv2/urid/urid.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace rb::pluginhost
{

/// **The LV2 world a host loads its plugin from**, with the URID map every
/// LV2 plugin asks for. One per host, and a host runs one plugin.
class Lv2World
{
public:
    Lv2World();
    ~Lv2World();
    Lv2World(const Lv2World &) = delete;
    Lv2World &operator=(const Lv2World &) = delete;

    [[nodiscard]] LilvWorld *world() const noexcept
    {
        return m_world;
    }
    [[nodiscard]] const LilvPlugin *plugin(const std::string &uri) const;

    [[nodiscard]] LV2_URID map(const char *uri);
    [[nodiscard]] const char *unmap(LV2_URID urid);

    [[nodiscard]] LV2_URID_Map *mapFeature() noexcept
    {
        return &m_map;
    }
    [[nodiscard]] LV2_URID_Unmap *unmapFeature() noexcept
    {
        return &m_unmap;
    }

private:
    LilvWorld *m_world = nullptr;
    std::mutex m_lock;
    std::unordered_map<std::string, LV2_URID> m_ids;
    std::vector<std::string> m_uris;
    LV2_URID_Map m_map{};
    LV2_URID_Unmap m_unmap{};
};

/// **An LV2 plugin, run by the host as a block**.
///
/// Audio and control ports, v1: an atom port is given an empty sequence in
/// and room for one out, and a CV port is given silence, so a plugin that
/// has them runs but says nothing through them. The control inputs are the
/// block's parameters in port order — the order the scan described them
/// in, which is how the engine numbers the changes it sends. A choice is
/// sent as its position among the scale points, and becomes the value here.
///
/// **A mono plugin on a stereo lane** runs twice, once a side, or once on
/// the two sides mixed; a mono output is sent to both sides.
///
/// **Its state is lilv's**: the control values and whatever the plugin
/// keeps through the state extension, as Turtle text, which is what the
/// session file holds for it.
class Lv2Plugin final : public dsp::Block
{
public:
    enum class Mono : std::uint8_t {
        DualMono,
        Downmix,
    };

    /// The plugin \a uri, or nothing when this machine has not got it.
    [[nodiscard]] static std::unique_ptr<Lv2Plugin>
    make(const std::shared_ptr<Lv2World> &world, const std::string &uri, Mono mono);

    ~Lv2Plugin() override;

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

    /// The plugin's whole state as Turtle, and putting it back. Before
    /// prepare() a state is kept and put in once the plugin is made.
    [[nodiscard]] std::string stateText() const;
    [[nodiscard]] bool loadStateText(const std::string &text);

    /// Takes the latency the plugin last reported onto info(). The host's
    /// main thread, which is the only one that reads info().
    void refreshLatency() noexcept;

    /// How many instances run: two for a mono plugin run once a side.
    [[nodiscard]] std::size_t instanceCount() const noexcept
    {
        return m_instances.size();
    }

private:
    Lv2Plugin(std::shared_ptr<Lv2World> world, const LilvPlugin *plugin, Mono mono);

    struct Port {
        enum class Kind : std::uint8_t {
            AudioIn,
            AudioOut,
            ControlIn,
            ControlOut,
            AtomIn,
            AtomOut,
            Other,
        };
        Kind kind = Kind::Other;
        std::uint32_t index = 0;
        /// For a control input: which parameter it is.
        std::uint32_t param = 0;
    };

    void connectFixedPorts(LilvInstance *instance, std::size_t which);
    /// Sets parameter \a index to \a value itself, in every instance.
    void setValue(std::uint32_t index, float value) noexcept RB_NONBLOCKING;
    void freeInstances();

    std::shared_ptr<Lv2World> m_world;
    const LilvPlugin *m_plugin = nullptr;
    Mono m_mono = Mono::DualMono;
    dsp::BlockInfo m_info;
    std::vector<plugin::PluginParam> m_params;
    /// Per parameter, a choice's distinct values in order: what a position
    /// the engine sends means. Worked out here, not on the audio thread.
    std::vector<std::vector<float>> m_choices;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    std::vector<Port> m_ports;
    std::uint32_t m_audioIns = 0;
    std::uint32_t m_audioOuts = 0;
    std::uint32_t m_latencyPort = UINT32_MAX;

    std::vector<LilvInstance *> m_instances;
    /// Per instance, a value for every port index: controls, and the
    /// latency the plugin writes.
    std::vector<std::vector<float>> m_controls;
    /// What the controls are, where the main thread can read them.
    std::vector<std::atomic<float>> m_values;
    /// Per instance, per atom port, a buffer.
    std::vector<std::vector<std::vector<std::uint64_t>>> m_atoms;
    std::vector<float> m_silence;
    std::vector<float> m_mix;
    std::vector<float> m_spare;
    std::atomic<std::uint32_t> m_latency{0};
    double m_rate = 0.0;
    std::string m_pendingState;
    LV2_URID m_sequence = 0;
    LV2_URID m_chunk = 0;
    LV2_Feature m_mapFeature{};
    LV2_Feature m_unmapFeature{};
    std::vector<const LV2_Feature *> m_features;
};

} // namespace rb::pluginhost

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "plugin/formats.h"

#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace rb::pluginhost
{

/// **One `.clap` file, loaded**: its entry initialised for as long as
/// the host runs, and the factory its plugins are made by. One per host, and
/// a host runs one plugin.
class ClapLibrary
{
public:
    /// The file at \a path, or nothing when it is not a CLAP plugin file
    /// this host can load.
    [[nodiscard]] static std::shared_ptr<ClapLibrary> open(const std::string &path);

    ~ClapLibrary();
    ClapLibrary(const ClapLibrary &) = delete;
    ClapLibrary &operator=(const ClapLibrary &) = delete;

    [[nodiscard]] std::uint32_t count() const;
    [[nodiscard]] const clap_plugin_descriptor *descriptorAt(std::uint32_t index) const;
    /// A plugin of the file's called \a id, made and initialised, or null.
    [[nodiscard]] const clap_plugin *create(const clap_host *host, std::string_view id) const;

private:
    ClapLibrary() = default;

    void *m_handle = nullptr;
    const clap_plugin_entry *m_entry = nullptr;
    const clap_plugin_factory *m_factory = nullptr;
};

/// **What this host is to a CLAP plugin**, and what the plugin has asked of
/// it: a call on the main thread, or to be restarted.
///
/// The host extensions a plugin can count on are here: log, thread check,
/// parameters, latency, state and audio ports. What they ask for is kept as
/// a flag for the main thread; nothing is done on the thread that asked.
class ClapHost
{
public:
    ClapHost();
    ClapHost(const ClapHost &) = delete;
    ClapHost &operator=(const ClapHost &) = delete;

    [[nodiscard]] const clap_host *host() const noexcept
    {
        return &m_host;
    }

    /// Whether the plugin asked for on_main_thread(), taken back.
    [[nodiscard]] bool takeCallback() noexcept
    {
        return m_callback.exchange(false);
    }
    /// Whether the plugin asked to be deactivated and activated again,
    /// taken back.
    [[nodiscard]] bool takeRestart() noexcept
    {
        return m_restart.exchange(false);
    }

    /// Marks the calling thread as acting for the audio thread while it
    /// exists: stop_processing() is called on the main thread, once the
    /// audio thread has let go of the plugin, and the thread check says so.
    class ActingAsAudio
    {
    public:
        ActingAsAudio();
        ~ActingAsAudio();
        ActingAsAudio(const ActingAsAudio &) = delete;
        ActingAsAudio &operator=(const ActingAsAudio &) = delete;
    };

private:
    static const void *extension(const clap_host *host, const char *id);
    static ClapHost *self(const clap_host *host);

    clap_host m_host{};
    std::thread::id m_main;
    std::atomic<bool> m_callback{false};
    std::atomic<bool> m_restart{false};
};

/// **A CLAP plugin's settings, as the engine numbers them**: every
/// parameter that is neither hidden nor read-only, in the plugin's order,
/// with \a ids and \a cookies (when given) filled in the same order.
///
/// CLAP says only whether a parameter is stepped; what the steps are is
/// found from the words the plugin gives its values. A stepped parameter
/// whose whole numbers are all numbers is a whole number; one of two values
/// called off and on is a switch; otherwise it is a choice of the values
/// its words change at. Some plugins (LSP's among them) step a parameter
/// through fractions of 0 to 1, which the words find as well as whole
/// numbers.
[[nodiscard]] std::vector<plugin::PluginParam>
clapParams(const clap_plugin *plugin, std::vector<clap_id> *ids = nullptr, std::vector<void *> *cookies = nullptr);

/// **Every plugin in the `.clap` file at \a path**, described: each is made
/// and asked, then let go. \a found is told of each as soon as it is
/// described, so a large file shows its progress to whoever waits on it.
/// False when the file cannot be loaded.
[[nodiscard]] bool describeClap(const std::string &path,
                                const std::function<void(const plugin::PluginDescription &)> &found);

/// **A CLAP plugin, run by the host as a block**.
///
/// Its audio ports' channels, in port order, are the block's ports, as an
/// LV2 plugin's audio ports are. **A mono plugin on a stereo lane** runs
/// twice, once a side, or once on the two sides mixed; a mono output is
/// sent to both sides.
///
/// **A change reaches the plugin as an event** in the next process call,
/// which is how CLAP takes them, and a value the plugin reports is taken as
/// what the setting is. A choice is sent as its position among the values
/// clapParams() found, and becomes the value here.
///
/// **Its state is the plugin's own**, through the state extension: bytes,
/// kept in the session as base64.
class ClapPlugin final : public dsp::Block
{
public:
    enum class Mono : std::uint8_t {
        DualMono,
        Downmix,
    };

    /// The plugin \a id of \a library, or nothing when the file has not got
    /// it or it would not start.
    [[nodiscard]] static std::unique_ptr<ClapPlugin>
    make(const std::shared_ptr<ClapLibrary> &library, const std::string &id, Mono mono);

    ~ClapPlugin() override;
    ClapPlugin(const ClapPlugin &) = delete;
    ClapPlugin &operator=(const ClapPlugin &) = delete;

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

    /// The plugin's whole state, as base64, and putting it back. Before
    /// prepare() a state is kept and put in once the plugin is running.
    [[nodiscard]] std::string stateText() const;
    [[nodiscard]] bool loadStateText(const std::string &text);

    /// Takes the latency the plugin reports now onto info(). The host's
    /// main thread, which is the only one that reads info().
    void refreshLatency() noexcept;

    /// **The main thread's turn**: the calls the plugin asked for there.
    /// True when it asked to be restarted, which the host does by making it
    /// again with its state.
    [[nodiscard]] bool idle();

    /// How many instances run: two for a mono plugin run once a side.
    [[nodiscard]] std::size_t instanceCount() const noexcept
    {
        return m_instances.size();
    }

private:
    ClapPlugin(std::shared_ptr<ClapLibrary> library, std::string id, Mono mono);

    /// Sets parameter \a index to \a value itself, where the main thread and
    /// the state can read it.
    void remember(std::uint32_t index, float value) noexcept RB_NONBLOCKING;
    /// Hands the values set before the plugin was running to every
    /// instance, on the main thread.
    void flushValues();
    /// Reads every value back from the plugin: after a state is loaded.
    void readValues();
    void freeInstances();

    std::shared_ptr<ClapLibrary> m_library;
    std::string m_id;
    Mono m_mono = Mono::DualMono;
    std::unique_ptr<ClapHost> m_host;
    dsp::BlockInfo m_info;
    std::vector<plugin::PluginParam> m_params;
    std::vector<clap_id> m_ids;
    std::vector<void *> m_cookies;
    /// The parameters' ids in order with their index, for a value the
    /// plugin reports by id on the audio thread.
    std::vector<std::pair<clap_id, std::uint32_t>> m_byId;
    /// Per parameter, a choice's values in order: what a position means.
    std::vector<std::vector<float>> m_choices;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    /// What the parameters are, where the main thread can read them.
    std::vector<std::atomic<float>> m_values;

    /// The channels of each of the plugin's audio ports, in order.
    std::vector<std::uint32_t> m_inputPorts;
    std::vector<std::uint32_t> m_outputPorts;
    std::uint32_t m_audioIns = 0;
    std::uint32_t m_audioOuts = 0;

    std::vector<const clap_plugin *> m_instances;
    bool m_active = false;
    /// Per instance, whether start_processing() has been called: the audio
    /// thread's, until the instance goes.
    std::vector<std::uint8_t> m_processing;
    std::int64_t m_steadyTime = 0;

    /// **The changes of this cycle**, gathered on the audio thread: which
    /// parameters, in the order they changed, and the events that say so.
    std::vector<std::uint8_t> m_changed;
    std::vector<std::uint32_t> m_changedList;
    std::uint32_t m_changedCount = 0;
    std::vector<clap_event_param_value> m_events;
    std::uint32_t m_eventCount = 0;

    /// Per instance, per port, a buffer description and its channels.
    std::vector<std::vector<clap_audio_buffer>> m_inBuffers;
    std::vector<std::vector<clap_audio_buffer>> m_outBuffers;
    std::vector<std::vector<float *>> m_inChannels;
    std::vector<std::vector<float *>> m_outChannels;
    std::vector<float> m_silence;
    std::vector<float> m_mix;
    std::vector<float> m_spare;

    std::string m_pendingState;
};

} // namespace rb::pluginhost

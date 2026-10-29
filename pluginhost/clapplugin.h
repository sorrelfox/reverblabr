// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "dsp/block.h"
#include "editor.h"
#include "plugin/formats.h"
#include "rt/spscqueue.h"

#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace rb::pluginhost
{

class X11Window;

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
    /// The file's preset-discovery factory, or null when it has none.
    [[nodiscard]] const clap_preset_discovery_factory *presetFactory() const;
    /// A plugin of the file's called \a id, made and initialised, or null.
    [[nodiscard]] const clap_plugin *create(const clap_host *host, std::string_view id) const;

private:
    ClapLibrary() = default;

    void *m_handle = nullptr;
    const clap_plugin_entry *m_entry = nullptr;
    const clap_plugin_factory *m_factory = nullptr;
};

/// **What this host is to a CLAP plugin**, and what the plugin has asked of
/// it: a call on the main thread, to be restarted, its values read again,
/// its window resized or shown or closed, a preset it has put in.
///
/// The host extensions a plugin can count on are here: log, thread check,
/// parameters, latency, state, audio ports, gui, timer support, posix fd
/// support and preset load. What the plugin asks for from another thread is
/// kept as a flag for the main thread; what it asks of the main thread (its
/// window, its timers) is done there and then. **One for each instance**, so
/// a timer is called on the instance that asked for it.
class ClapHost
{
public:
    ClapHost();
    ~ClapHost();
    ClapHost(const ClapHost &) = delete;
    ClapHost &operator=(const ClapHost &) = delete;

    [[nodiscard]] const clap_host *host() const noexcept
    {
        return &m_host;
    }

    /// The instance this host is to, once made.
    void setPlugin(const clap_plugin *plugin) noexcept
    {
        m_plugin = plugin;
    }

    /// **Where its timers and descriptors are watched.** A plugin may ask
    /// for them before there is a loop (as it is made): they are kept and
    /// watched from here on. Null stops watching them.
    void setLoop(MainLoop *loop);

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
    /// Whether the plugin said its values changed by themselves (a preset
    /// in its window), taken back.
    [[nodiscard]] bool takeRescan() noexcept
    {
        return m_rescan.exchange(false);
    }
    /// Whether the plugin's floating window was closed by a person, taken
    /// back.
    [[nodiscard]] bool takeClosed() noexcept
    {
        return m_closed.exchange(false);
    }
    /// The preset the plugin said it has put in, as "<kind> <location> <key>",
    /// taken back; or nothing.
    [[nodiscard]] std::optional<std::string> takeLoaded();

    /// **What the plugin's window asks of the window it is in**: called on
    /// the main thread as it asks. Unset, a request is refused.
    std::function<bool(std::uint32_t, std::uint32_t)> requestResize;
    std::function<bool(bool)> requestShow;
    std::function<void()> resizeHintsChanged;

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

    struct Timer {
        std::uint32_t period = 0;
        std::optional<MainLoop::Id> watch;
    };
    struct Fd {
        clap_posix_fd_flags_t flags = 0;
        std::optional<MainLoop::Id> watch;
    };
    void watchTimer(clap_id id, Timer &timer);
    void watchFd(int fd, Fd &watched);

    clap_host m_host{};
    std::thread::id m_main;
    const clap_plugin *m_plugin = nullptr;
    MainLoop *m_loop = nullptr;
    std::map<clap_id, Timer> m_timers;
    clap_id m_nextTimer = 0;
    std::map<int, Fd> m_fds;
    std::atomic<bool> m_callback{false};
    std::atomic<bool> m_restart{false};
    std::atomic<bool> m_rescan{false};
    std::atomic<bool> m_closed{false};
    std::mutex m_loadedLock;
    std::optional<std::string> m_loaded;
};

/// **What a CLAP plugin's window did, as its audio thread heard it**, for
/// the main thread: a parameter moved, or taken hold of, or let go of.
struct ClapSaid {
    enum Kind : std::uint8_t {
        Value,
        Begin,
        End,
    };
    Kind kind = Value;
    std::uint32_t index = 0;
    float value = 0.0f;
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
///
/// **Its own window is the gui extension's**: an X11 window of its own,
/// floating, when it offers one; otherwise put inside an X11 window the
/// host makes; a Wayland one where it offers that and the desktop is
/// Wayland's. A parameter it moves arrives as an output event, gestures
/// round it, and goes to the engine too; one the engine moves reaches it as
/// an event, as ever, and its window follows by itself.
///
/// **Its presets are its own**, found through its preset-discovery factory
/// and put in through preset-load. One kept from a CLAP block is ours,
/// with the plugin's state in it.
class ClapPlugin final : public dsp::Block, public Editable
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

    [[nodiscard]] Opened openUi() override;
    void closeUi() override;
    [[nodiscard]] bool uiOpen() const override
    {
        return m_gui != nullptr;
    }
    [[nodiscard]] bool hasUi() const override;
    [[nodiscard]] std::vector<plugin::PresetEntry> presets() override;
    [[nodiscard]] bool loadPreset(const std::string &id) override;

    /// **The main thread's turn for what the window and presets did**: a
    /// parameter the window moved, values the plugin read again, a preset
    /// put in, a window closed. A few times a second; a test calls it.
    void editorTick();

protected:
    void onAttached() override;

private:
    ClapPlugin(std::shared_ptr<ClapLibrary> library, std::string id, Mono mono);

    /// One of the plugin's own presets, as its preset discovery told of it.
    struct Found {
        std::uint32_t kind = 0;
        std::string location;
        std::string key;
    };
    [[nodiscard]] static std::string presetId(std::uint32_t kind, const std::string &location, const std::string &key);
    /// Makes the window's requests reach the window it is in.
    void hookWindow(ClapHost &host);

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
    std::string m_name;
    Mono m_mono = Mono::DualMono;
    /// One host for each instance, the first instance's first.
    std::vector<std::unique_ptr<ClapHost>> m_hosts;
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

    /// **What the window does, for the main thread**: the first
    /// instance's output events, while its window is open.
    rt::SpscQueue<ClapSaid> m_said{1024};
    std::atomic<bool> m_windowListens{false};
    std::optional<MainLoop::Id> m_tick;
    const clap_plugin_gui *m_gui = nullptr;
    bool m_floating = false;
    std::unique_ptr<X11Window> m_window;
    /// The presets the last listing found, by the ids it gave them.
    std::map<std::string, Found> m_found;
    /// A preset asked for and not yet in, and how many turns it has been
    /// waited for.
    std::optional<std::string> m_loading;
    int m_loadingFor = 0;
};

} // namespace rb::pluginhost

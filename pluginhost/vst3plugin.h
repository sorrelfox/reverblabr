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
#include "vst3module.h"

#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstevents.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/ivstprocesscontext.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// **The format's interfaces have no virtual destructors**: an object is
// let go through release(), never deleted through one of them, so the
// warning about it is not for these classes.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace rb::pluginhost
{

/// **Where a VST3 plugin's view is put**: a top-level window of the host's,
/// an X11 one; or, in a test, a stand-in that needs no display.
class Vst3ViewParent
{
public:
    virtual ~Vst3ViewParent() = default;
    /// The X window the view is made inside.
    [[nodiscard]] virtual unsigned long id() const = 0;
    virtual void resize(int width, int height) = 0;
    virtual void setResizable(bool resizable) = 0;
    virtual void show() = 0;
};

/// Makes a parent titled \a title, for the plugin \a key names, at
/// \a width by \a height; nothing when there is nowhere to show it.
/// \a closed and \a resized are as X11Window::open() has them.
using Vst3ViewParents = std::function<std::unique_ptr<Vst3ViewParent>(MainLoop &loop,
                                                                      const std::string &title,
                                                                      const std::string &key,
                                                                      int width,
                                                                      int height,
                                                                      std::function<void(bool)> closed,
                                                                      std::function<void(int, int)> resized)>;

/// **One cycle's parameter changes**, as a VST3 plugin is given them and
/// gives its own: a queue a parameter, each of a few points, all made
/// before the audio thread starts, so that nothing is allocated on it.
class Vst3Changes final : public Steinberg::Vst::IParameterChanges
{
public:
    /// Room for \a parameters parameters a cycle.
    explicit Vst3Changes(std::size_t parameters);
    Vst3Changes(const Vst3Changes &) = delete;
    Vst3Changes &operator=(const Vst3Changes &) = delete;

    void clear() noexcept RB_NONBLOCKING;
    /// Parameter \a id at \a value from the cycle's first sample, in its
    /// one queue of the cycle; false when there is no room.
    bool add(Steinberg::Vst::ParamID id, double value) noexcept RB_NONBLOCKING;
    /// The parameters given, and the last value each was given.
    [[nodiscard]] std::size_t count() const noexcept RB_NONBLOCKING
    {
        return m_used;
    }
    [[nodiscard]] Steinberg::Vst::ParamID idAt(std::size_t index) const noexcept RB_NONBLOCKING;
    [[nodiscard]] std::optional<double> lastAt(std::size_t index) const noexcept RB_NONBLOCKING;

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::int32 PLUGIN_API getParameterCount() override;
    Steinberg::Vst::IParamValueQueue *PLUGIN_API getParameterData(Steinberg::int32 index) override;
    Steinberg::Vst::IParamValueQueue *PLUGIN_API addParameterData(const Steinberg::Vst::ParamID &id,
                                                                  Steinberg::int32 &index) override;

private:
    class Queue final : public Steinberg::Vst::IParamValueQueue
    {
    public:
        static constexpr std::size_t kPoints = 16;

        Queue() = default;
        Queue(const Queue &) = default;
        Queue &operator=(const Queue &) = default;

        void start(Steinberg::Vst::ParamID id) noexcept RB_NONBLOCKING
        {
            m_id = id;
            m_count = 0;
        }
        [[nodiscard]] std::optional<double> last() const noexcept RB_NONBLOCKING
        {
            return m_count > 0 ? std::optional<double>(m_points[m_count - 1].second) : std::nullopt;
        }
        [[nodiscard]] Steinberg::Vst::ParamID id() const noexcept RB_NONBLOCKING
        {
            return m_id;
        }
        /// A point at \a offset, and where it went. A point at the same
        /// place replaces the last; one past the room there is moves the
        /// last, so the last value always stands.
        std::size_t put(Steinberg::int32 offset, double value) noexcept RB_NONBLOCKING
        {
            if (m_count > 0 && m_points[m_count - 1].first == offset) {
                m_points[m_count - 1].second = value;
                return m_count - 1;
            }
            if (m_count >= kPoints) {
                m_points[kPoints - 1] = {offset, value};
                return kPoints - 1;
            }
            m_points[m_count] = {offset, value};
            return m_count++;
        }

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override;
        Steinberg::uint32 PLUGIN_API addRef() override;
        Steinberg::uint32 PLUGIN_API release() override;
        Steinberg::Vst::ParamID PLUGIN_API getParameterId() override;
        Steinberg::int32 PLUGIN_API getPointCount() override;
        Steinberg::tresult PLUGIN_API getPoint(Steinberg::int32 index,
                                               Steinberg::int32 &sampleOffset,
                                               Steinberg::Vst::ParamValue &value) override;
        Steinberg::tresult PLUGIN_API addPoint(Steinberg::int32 sampleOffset,
                                               Steinberg::Vst::ParamValue value,
                                               Steinberg::int32 &index) override;

    private:
        Steinberg::Vst::ParamID m_id = 0;
        std::size_t m_count = 0;
        std::array<std::pair<Steinberg::int32, double>, kPoints> m_points{};
    };

    /// The queue of parameter \a id this cycle, made if it has none; null
    /// when there is no room.
    [[nodiscard]] Queue *find(Steinberg::Vst::ParamID id) noexcept RB_NONBLOCKING;

    std::vector<Queue> m_queues;
    std::size_t m_used = 0;
};

/// **A value a VST3 plugin's window set**, as the plugin has it, for its
/// sound.
struct Vst3Edit {
    Steinberg::Vst::ParamID id = 0;
    double value = 0.0;
};

/// **A VST3 plugin, run by the host as a block** (ADR-0028).
///
/// The channels of its first main audio bus each way are the block's ports;
/// its other buses (a side chain) hear silence. **A mono plugin on a stereo
/// lane** runs twice, once a side, or once on the two sides mixed, as a
/// CLAP plugin does; the second instance is a component alone, given the
/// first one's state, every change the first is given and every message
/// the controller sends.
///
/// **A setting is the plain value the plugin shows**, and the plugin is
/// told the 0 to 1 it maps to (Vst3Curve), as a parameter change in the
/// next process call. A choice arrives as its position among the values,
/// as it does for the other formats.
///
/// **Its state is both halves'**, the component's and the controller's, in
/// a `.vstpreset` of its own, kept in the session as base64. **Its presets
/// are `.vstpreset` files** where other hosts keep them, and one saved
/// from the block goes to the user's folder of them.
///
/// **Its own window is its editor view**, put inside an X11 window the
/// host makes; the view's timers and descriptors run on the host's main
/// loop, through the run loop the window offers it. What a person does
/// there arrives through beginEdit, performEdit and endEdit on the main
/// thread, goes to the audio thread through the host's queue and to the
/// engine; what the engine or the plugin's sound changes is told to the
/// controller on the host's turn, thirty times a second.
class Vst3Plugin final : public dsp::Block, public Editable
{
public:
    enum class Mono : std::uint8_t {
        DualMono,
        Downmix,
    };

    /// Class \a id of \a module, or nothing when the bundle has not got it
    /// or it would not start.
    [[nodiscard]] static std::unique_ptr<Vst3Plugin>
    make(const std::shared_ptr<Vst3Module> &module, const std::string &id, Mono mono);

    /// **Where views go from now on**: for a test, parents of its own that
    /// need no display. Empty puts them in X11 windows again.
    static void setViewParents(Vst3ViewParents parents);

    ~Vst3Plugin() override;
    Vst3Plugin(const Vst3Plugin &) = delete;
    Vst3Plugin &operator=(const Vst3Plugin &) = delete;

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

    /// The plugin's whole state, as base64, and putting it back.
    [[nodiscard]] std::string stateText() const;
    [[nodiscard]] bool loadStateText(const std::string &text);

    /// Takes the latency the plugin reports now onto info().
    void refreshLatency() noexcept;

    /// **The main thread's turn**: a new latency taken on, by activating
    /// the plugin again; true when it asked to be made again (its buses
    /// changed), which the host does with its state.
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
        return m_view.get() != nullptr;
    }
    [[nodiscard]] bool hasUi() const override;
    [[nodiscard]] std::vector<plugin::PresetEntry> presets() override;
    [[nodiscard]] bool loadPreset(const std::string &id) override;
    [[nodiscard]] std::optional<std::string> savePreset(const std::string &name) override;

    /// **The main thread's turn for the controller**: what changed
    /// elsewhere told to it, values it changed itself read back. A few
    /// times a second; a test calls it.
    void editorTick();

protected:
    void onAttached() override;

private:
    Vst3Plugin(std::shared_ptr<Vst3Module> module, std::string id, Mono mono);

    /// **What the host is to the controller**: told of what a person does in
    /// the window, and of the plugin wanting to be read again or restarted.
    class Handler final : public Steinberg::Vst::IComponentHandler, public Steinberg::Vst::IComponentHandler2
    {
    public:
        explicit Handler(Vst3Plugin &plugin)
            : m_plugin(plugin)
        {
        }
        Handler(const Handler &) = delete;
        Handler &operator=(const Handler &) = delete;

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override;
        Steinberg::uint32 PLUGIN_API addRef() override;
        Steinberg::uint32 PLUGIN_API release() override;
        Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID id) override;
        Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                                  Steinberg::Vst::ParamValue value) override;
        Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID id) override;
        Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 flags) override;
        Steinberg::tresult PLUGIN_API setDirty(Steinberg::TBool state) override;
        Steinberg::tresult PLUGIN_API requestOpenEditor(Steinberg::FIDString name) override;
        Steinberg::tresult PLUGIN_API startGroupEdit() override;
        Steinberg::tresult PLUGIN_API finishGroupEdit() override;

    private:
        Vst3Plugin &m_plugin;
    };

    /// **What the host is to the view**: the window it is in, which it may
    /// ask to be resized, and the run loop its timers and descriptors are
    /// on.
    class Frame final : public Steinberg::IPlugFrame, public Steinberg::Linux::IRunLoop
    {
    public:
        explicit Frame(Vst3Plugin &plugin)
            : m_plugin(plugin)
        {
        }
        Frame(const Frame &) = delete;
        Frame &operator=(const Frame &) = delete;

        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override;
        Steinberg::uint32 PLUGIN_API addRef() override;
        Steinberg::uint32 PLUGIN_API release() override;
        Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView *view, Steinberg::ViewRect *size) override;
        Steinberg::tresult PLUGIN_API registerEventHandler(Steinberg::Linux::IEventHandler *handler,
                                                           Steinberg::Linux::FileDescriptor fd) override;
        Steinberg::tresult PLUGIN_API unregisterEventHandler(Steinberg::Linux::IEventHandler *handler) override;
        Steinberg::tresult PLUGIN_API registerTimer(Steinberg::Linux::ITimerHandler *handler,
                                                    Steinberg::Linux::TimerInterval milliseconds) override;
        Steinberg::tresult PLUGIN_API unregisterTimer(Steinberg::Linux::ITimerHandler *handler) override;

        /// Lets go of every timer and descriptor the view left behind.
        void clear();
        /// How many timers and descriptors the view has on the loop.
        [[nodiscard]] std::size_t watches() const noexcept
        {
            return m_watches.size();
        }

    private:
        struct Watch {
            /// What is let go when the watch is.
            Steinberg::FUnknown *handler = nullptr;
            /// The handler as it was given, which names the watch: an
            /// object may be both kinds, at two addresses.
            const void *key = nullptr;
            bool timer = false;
            MainLoop::Id id = 0;
        };
        Steinberg::tresult drop(const void *key, bool timer);
        /// Lets go of what was unregistered inside a call of its own, once
        /// that call is over.
        void settle();

        Vst3Plugin &m_plugin;
        std::vector<Watch> m_watches;
        std::vector<Steinberg::FUnknown *> m_letGo;
        int m_calling = 0;
    };

    /// One instance of the plugin's sound, and its buses as the process
    /// call is given them.
    struct Instance {
        Vst3Ref<Steinberg::Vst::IComponent> component;
        Vst3Ref<Steinberg::Vst::IAudioProcessor> processor;
        std::vector<Steinberg::Vst::AudioBusBuffers> inputs;
        std::vector<Steinberg::Vst::AudioBusBuffers> outputs;
        /// Each bus's channels, one after another; the first bus's first.
        std::vector<float *> inChannels;
        std::vector<float *> outChannels;
        std::uint32_t mainIns = 0;
        std::uint32_t mainOuts = 0;
        /// Which bus each way is the block's: the first main one, wherever
        /// it is among the buses; none when there is none.
        std::size_t mainIn = kNoBus;
        std::size_t mainOut = kNoBus;
        bool processing = false;
        /// A second instance's connection point, and the relay it sends
        /// into, which passes nothing on: the first instance speaks to the
        /// controller for both. It hears the controller all the same.
        Vst3Ref<Steinberg::Vst::IConnectionPoint> point;
        std::unique_ptr<Vst3Relay> relay;
    };
    static constexpr std::size_t kNoBus = static_cast<std::size_t>(-1);

    /// **A value the plugin's sound gave out**, for the controller: what a
    /// meter shows, or a setting the plugin moved itself.
    struct Given {
        Steinberg::Vst::ParamID id = 0;
        std::atomic<double> value{0.0};
        std::atomic<bool> fresh{false};
    };

    /// Sets parameter \a index to plain \a value itself, where the main
    /// thread and the state can read it.
    void remember(std::uint32_t index, float value) noexcept RB_NONBLOCKING;
    /// The index of the parameter the plugin calls \a id, or nothing.
    [[nodiscard]] std::optional<std::uint32_t> indexOf(Steinberg::Vst::ParamID id) const noexcept RB_NONBLOCKING;
    /// \a value as the block's parameter takes it: a choice's position.
    [[nodiscard]] float blockValue(std::uint32_t index, float value) const noexcept;
    /// Reads every value back from the controller, which is then told of
    /// none of them.
    void readValues();
    /// Puts \a preset into every instance and the controller.
    [[nodiscard]] bool apply(const Vst3Preset &preset);
    [[nodiscard]] Vst3Preset capture() const;
    /// Makes instance \a which ready to run at \a spec.
    [[nodiscard]] bool start(Instance &instance, const dsp::PrepareSpec &spec);
    void stop(Instance &instance);
    /// Lets go of the second instance of a mono plugin run twice.
    void dropSecond();
    /// Deactivates and activates again every instance, with the audio
    /// thread kept away, which is how the format has a host take on a new
    /// latency.
    void reactivate();

    void edited(Steinberg::Vst::ParamID id, double normalized);
    void gesture(Steinberg::Vst::ParamID id, bool begin);

    std::shared_ptr<Vst3Module> m_module;
    std::string m_id;
    std::string m_name;
    std::string m_vendor;
    Mono m_mono = Mono::DualMono;
    Handler m_handler{*this};
    Frame m_frame{*this};
    std::optional<Vst3Halves> m_halves;
    std::vector<Instance> m_instances;
    bool m_active = false;
    dsp::BlockInfo m_info;
    double m_sampleRate = 0.0;

    std::vector<plugin::PluginParam> m_params;
    std::vector<Steinberg::Vst::ParamID> m_ids;
    std::vector<Vst3Curve> m_curves;
    /// The parameters' ids in order with their index.
    std::vector<std::pair<Steinberg::Vst::ParamID, std::uint32_t>> m_byId;
    /// Per parameter, a choice's values in order: what a position means.
    std::vector<std::vector<float>> m_choices;
    std::vector<dsp::ParamDescriptor> m_descriptors;
    /// What the parameters are, plain, where the main thread can read them.
    std::vector<std::atomic<float>> m_values;
    /// What the controller was last told each is, on the main thread.
    std::vector<float> m_told;
    /// Every parameter the controller has, by id, for what the sound gives
    /// out.
    std::vector<Given> m_given;

    /// **The changes of this cycle**, gathered on the audio thread.
    std::vector<std::uint8_t> m_changed;
    std::vector<std::uint32_t> m_changedList;
    std::uint32_t m_changedCount = 0;
    /// A setting the window moved that the block does not show (hidden),
    /// which reaches the sound all the same.
    rt::SpscQueue<Vst3Edit> m_unlisted{256};
    std::unique_ptr<Vst3Changes> m_in;
    std::unique_ptr<Vst3Changes> m_out;
    std::unique_ptr<Vst3Changes> m_spareOut;

    class Events;
    std::unique_ptr<Events> m_events;
    Steinberg::Vst::ProcessContext m_context{};
    std::vector<float> m_silence;
    std::vector<float> m_mix;
    std::vector<float> m_spare;
    std::int64_t m_steadyTime = 0;

    std::atomic<bool> m_rescan{false};
    std::atomic<bool> m_restart{false};
    std::atomic<bool> m_latencyChanged{false};
    std::optional<MainLoop::Id> m_tick;
    Vst3Ref<Steinberg::IPlugView> m_view;
    std::unique_ptr<Vst3ViewParent> m_window;
};

} // namespace rb::pluginhost

#pragma GCC diagnostic pop

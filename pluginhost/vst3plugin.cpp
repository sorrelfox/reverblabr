// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "vst3plugin.h"

#include "base64.h"
#include "x11window.h"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

// As in the header: the format's interfaces are released, not deleted.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace rb::pluginhost
{

namespace fs = std::filesystem;
using namespace Steinberg;

namespace
{

[[nodiscard]] bool same(const TUID left, const TUID right)
{
    return FUnknownPrivate::iidEqual(left, right);
}

/// Where views go when a test has not said: X11 windows.
Vst3ViewParents &viewParents()
{
    static Vst3ViewParents parents;
    return parents;
}

/// **The host's X11 window, as a view's parent.**
class X11Parent final : public Vst3ViewParent
{
public:
    explicit X11Parent(std::unique_ptr<X11Window> window)
        : m_window(std::move(window))
    {
    }
    [[nodiscard]] unsigned long id() const override
    {
        return m_window->id();
    }
    void resize(int width, int height) override
    {
        m_window->resize(width, height);
    }
    void setResizable(bool resizable) override
    {
        m_window->setResizable(resizable);
    }
    void show() override
    {
        m_window->show();
    }

private:
    std::unique_ptr<X11Window> m_window;
};

/// **The plugin's own process call**, which the format puts on the audio
/// thread: made for it, which the compiler cannot know of a call through a
/// plugin's object. The claim is made here, once, as for the other
/// formats.
tresult callProcess(Vst::IAudioProcessor *processor, Vst::ProcessData *data)
{
    return processor->process(*data);
}

tresult processOf(Vst::IAudioProcessor *processor, Vst::ProcessData &data) noexcept RB_NONBLOCKING
{
    using Call = tresult (*)(Vst::IAudioProcessor *, Vst::ProcessData *) RB_NONBLOCKING;
    const auto call = reinterpret_cast<Call>(&callProcess);
    return call(processor, &data);
}

/// Ports numbered as the engine's plugin block numbers them.
[[nodiscard]] dsp::PortSpec numbered(std::uint32_t count, std::string_view prefix)
{
    std::vector<std::string> labels;
    for (std::uint32_t index = 0; index < count; ++index) {
        labels.push_back(std::string{prefix} + std::to_string(index + 1));
    }
    return dsp::PortSpec::fixed(count, std::move(labels));
}

/// The distinct values of a choice, in order: what its position means.
[[nodiscard]] std::vector<float> choiceValues(const plugin::PluginParam &param)
{
    std::vector<float> values;
    for (const auto &[value, label] : param.choices) {
        if (values.empty() || values.back() != value) {
            values.push_back(value);
        }
    }
    return values;
}

/// The largest preset or state file read: a plugin that keeps samples in
/// its state may need much, but not without end.
constexpr std::uintmax_t kMostPresetBytes = 64U * 1024U * 1024U;

[[nodiscard]] std::optional<std::string> readFile(const fs::path &path)
{
    std::error_code error;
    if (!fs::is_regular_file(path, error) || fs::file_size(path, error) > kMostPresetBytes || error) {
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// Whether \a path is inside \a folder, both as they really are, links
/// followed.
[[nodiscard]] bool isInside(const fs::path &path, const fs::path &folder)
{
    std::error_code error;
    const fs::path where = fs::canonical(path, error);
    if (error) {
        return false;
    }
    const fs::path root = fs::canonical(folder, error);
    if (error) {
        return false;
    }
    const auto [rootEnd, whereAt] = std::ranges::mismatch(root, where);
    return rootEnd == root.end() && whereAt != where.end();
}

} // namespace

/// **No notes, either way**: a block of this host is an effect, and the
/// plugin is given an empty list where one is asked for.
class Vst3Plugin::Events final : public Vst::IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
    {
        if (same(iid, FUnknown_iid) || same(iid, Vst::IEventList_iid)) {
            *object = static_cast<Vst::IEventList *>(this);
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    uint32 PLUGIN_API release() override
    {
        return 1;
    }
    int32 PLUGIN_API getEventCount() override
    {
        return 0;
    }
    tresult PLUGIN_API getEvent(int32, Vst::Event &) override
    {
        return kResultFalse;
    }
    tresult PLUGIN_API addEvent(Vst::Event &) override
    {
        return kResultFalse;
    }
};

// --- Vst3Changes ---

Vst3Changes::Vst3Changes(std::size_t parameters)
    : m_queues(std::max<std::size_t>(parameters, 1))
{
}

void Vst3Changes::clear() noexcept RB_NONBLOCKING
{
    m_used = 0;
}

bool Vst3Changes::add(Vst::ParamID id, double value) noexcept RB_NONBLOCKING
{
    // **One queue a parameter a cycle**, as the format has it: a second
    // value for the same parameter replaces the first.
    Queue *queue = find(id);
    if (queue == nullptr) {
        return false;
    }
    (void)queue->put(0, value);
    return true;
}

Vst3Changes::Queue *Vst3Changes::find(Vst::ParamID id) noexcept RB_NONBLOCKING
{
    for (std::size_t at = 0; at < m_used; ++at) {
        if (m_queues[at].id() == id) {
            return &m_queues[at];
        }
    }
    if (m_used >= m_queues.size()) {
        return nullptr;
    }
    Queue &queue = m_queues[m_used++];
    queue.start(id);
    return &queue;
}

Vst::ParamID Vst3Changes::idAt(std::size_t index) const noexcept RB_NONBLOCKING
{
    return index < m_used ? m_queues[index].id() : 0;
}

std::optional<double> Vst3Changes::lastAt(std::size_t index) const noexcept RB_NONBLOCKING
{
    return index < m_used ? m_queues[index].last() : std::nullopt;
}

tresult PLUGIN_API Vst3Changes::queryInterface(const TUID iid, void **object)
{
    if (same(iid, FUnknown_iid) || same(iid, Vst::IParameterChanges_iid)) {
        *object = static_cast<Vst::IParameterChanges *>(this);
        return kResultOk;
    }
    *object = nullptr;
    return kNoInterface;
}

// The host's own, for as long as the plugin runs.
uint32 PLUGIN_API Vst3Changes::addRef()
{
    return 1;
}

uint32 PLUGIN_API Vst3Changes::release()
{
    return 1;
}

int32 PLUGIN_API Vst3Changes::getParameterCount()
{
    return static_cast<int32>(m_used);
}

Vst::IParamValueQueue *PLUGIN_API Vst3Changes::getParameterData(int32 index)
{
    return index >= 0 && static_cast<std::size_t>(index) < m_used ? &m_queues[static_cast<std::size_t>(index)]
                                                                  : nullptr;
}

Vst::IParamValueQueue *PLUGIN_API Vst3Changes::addParameterData(const Vst::ParamID &id, int32 &index)
{
    Queue *queue = find(id);
    if (queue != nullptr) {
        index = static_cast<int32>(queue - m_queues.data());
    }
    return queue;
}

tresult PLUGIN_API Vst3Changes::Queue::queryInterface(const TUID iid, void **object)
{
    if (same(iid, FUnknown_iid) || same(iid, Vst::IParamValueQueue_iid)) {
        *object = static_cast<Vst::IParamValueQueue *>(this);
        return kResultOk;
    }
    *object = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API Vst3Changes::Queue::addRef()
{
    return 1;
}

uint32 PLUGIN_API Vst3Changes::Queue::release()
{
    return 1;
}

Vst::ParamID PLUGIN_API Vst3Changes::Queue::getParameterId()
{
    return m_id;
}

int32 PLUGIN_API Vst3Changes::Queue::getPointCount()
{
    return static_cast<int32>(m_count);
}

tresult PLUGIN_API Vst3Changes::Queue::getPoint(int32 index, int32 &sampleOffset, Vst::ParamValue &value)
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_count) {
        return kResultFalse;
    }
    sampleOffset = m_points[static_cast<std::size_t>(index)].first;
    value = m_points[static_cast<std::size_t>(index)].second;
    return kResultOk;
}

tresult PLUGIN_API Vst3Changes::Queue::addPoint(int32 sampleOffset, Vst::ParamValue value, int32 &index)
{
    index = static_cast<int32>(put(sampleOffset, value));
    return kResultOk;
}

// --- Vst3Plugin::Handler ---

tresult PLUGIN_API Vst3Plugin::Handler::queryInterface(const TUID iid, void **object)
{
    if (same(iid, FUnknown_iid) || same(iid, Vst::IComponentHandler_iid)) {
        *object = static_cast<Vst::IComponentHandler *>(this);
        return kResultOk;
    }
    if (same(iid, Vst::IComponentHandler2_iid)) {
        *object = static_cast<Vst::IComponentHandler2 *>(this);
        return kResultOk;
    }
    *object = nullptr;
    return kNoInterface;
}

// **The plugin's own**, living as long as it does.
uint32 PLUGIN_API Vst3Plugin::Handler::addRef()
{
    return 1;
}

uint32 PLUGIN_API Vst3Plugin::Handler::release()
{
    return 1;
}

tresult PLUGIN_API Vst3Plugin::Handler::beginEdit(Vst::ParamID id)
{
    m_plugin.gesture(id, true);
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::Handler::performEdit(Vst::ParamID id, Vst::ParamValue value)
{
    m_plugin.edited(id, value);
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::Handler::endEdit(Vst::ParamID id)
{
    m_plugin.gesture(id, false);
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::Handler::restartComponent(int32 flags)
{
    // **Its values moved by themselves** (a preset in its window): read
    // again on the main thread's next turn. Buses that changed mean a
    // plugin made again, which the host's turn does.
    if ((flags & Vst::kParamValuesChanged) != 0) {
        m_plugin.m_rescan.store(true);
    }
    if ((flags & (Vst::kReloadComponent | Vst::kIoChanged)) != 0) {
        m_plugin.m_restart.store(true);
    }
    // **A new latency**: the plugin is deactivated and activated again on
    // the host's turn, as the format asks, and its latency read then.
    if ((flags & Vst::kLatencyChanged) != 0) {
        m_plugin.m_latencyChanged.store(true);
    }
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::Handler::setDirty(TBool)
{
    // The engine keeps the session; it reads the state when it saves.
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::Handler::requestOpenEditor(FIDString)
{
    return kNotImplemented;
}

tresult PLUGIN_API Vst3Plugin::Handler::startGroupEdit()
{
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::Handler::finishGroupEdit()
{
    return kResultOk;
}

// --- Vst3Plugin::Frame ---

tresult PLUGIN_API Vst3Plugin::Frame::queryInterface(const TUID iid, void **object)
{
    if (same(iid, FUnknown_iid) || same(iid, IPlugFrame_iid)) {
        *object = static_cast<IPlugFrame *>(this);
        return kResultOk;
    }
    if (same(iid, Linux::IRunLoop_iid)) {
        *object = static_cast<Linux::IRunLoop *>(this);
        return kResultOk;
    }
    *object = nullptr;
    return kNoInterface;
}

uint32 PLUGIN_API Vst3Plugin::Frame::addRef()
{
    return 1;
}

uint32 PLUGIN_API Vst3Plugin::Frame::release()
{
    return 1;
}

tresult PLUGIN_API Vst3Plugin::Frame::resizeView(IPlugView *view, ViewRect *size)
{
    if (view == nullptr || size == nullptr || !m_plugin.m_window) {
        return kInvalidArgument;
    }
    // The window first, then the view told it has the room.
    m_plugin.m_window->resize(size->getWidth(), size->getHeight());
    (void)view->onSize(size);
    return kResultTrue;
}

tresult PLUGIN_API Vst3Plugin::Frame::registerEventHandler(Linux::IEventHandler *handler, Linux::FileDescriptor fd)
{
    MainLoop *loop = m_plugin.loop();
    if (handler == nullptr || fd < 0 || loop == nullptr) {
        return kInvalidArgument;
    }
    const std::optional<MainLoop::Id> watched = loop->addFd(fd, MainLoop::Readable, [this, handler, fd](std::uint32_t) {
        ++m_calling;
        handler->onFDIsSet(fd);
        --m_calling;
        settle();
    });
    if (!watched) {
        return kResultFalse;
    }
    handler->addRef();
    m_watches.push_back(Watch{handler, handler, false, *watched});
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::Frame::unregisterEventHandler(Linux::IEventHandler *handler)
{
    return drop(handler, false);
}

tresult PLUGIN_API Vst3Plugin::Frame::registerTimer(Linux::ITimerHandler *handler, Linux::TimerInterval milliseconds)
{
    MainLoop *loop = m_plugin.loop();
    if (handler == nullptr || loop == nullptr) {
        return kInvalidArgument;
    }
    const auto period = std::chrono::milliseconds{std::max<Linux::TimerInterval>(milliseconds, 1)};
    const std::optional<MainLoop::Id> watched = loop->addTimer(period, [this, handler] {
        ++m_calling;
        handler->onTimer();
        --m_calling;
        settle();
    });
    if (!watched) {
        return kResultFalse;
    }
    handler->addRef();
    m_watches.push_back(Watch{handler, handler, true, *watched});
    return kResultOk;
}

tresult PLUGIN_API Vst3Plugin::Frame::unregisterTimer(Linux::ITimerHandler *handler)
{
    return drop(handler, true);
}

tresult Vst3Plugin::Frame::drop(const void *key, bool timer)
{
    // **By the handler**: the format names a timer or a descriptor by what
    // it calls, and one handler may watch several descriptors.
    bool found = false;
    MainLoop *loop = m_plugin.loop();
    for (auto watch = m_watches.begin(); watch != m_watches.end();) {
        if (watch->key != key || watch->timer != timer) {
            ++watch;
            continue;
        }
        if (loop != nullptr) {
            loop->remove(watch->id);
        }
        m_letGo.push_back(watch->handler);
        watch = m_watches.erase(watch);
        found = true;
    }
    settle();
    return found ? kResultOk : kResultFalse;
}

void Vst3Plugin::Frame::settle()
{
    if (m_calling > 0) {
        return;
    }
    for (FUnknown *handler : std::exchange(m_letGo, {})) {
        handler->release();
    }
}

void Vst3Plugin::Frame::clear()
{
    MainLoop *loop = m_plugin.loop();
    for (const Watch &watch : std::exchange(m_watches, {})) {
        if (loop != nullptr) {
            loop->remove(watch.id);
        }
        m_letGo.push_back(watch.handler);
    }
    settle();
}

// --- Vst3Plugin ---

void Vst3Plugin::setViewParents(Vst3ViewParents parents)
{
    viewParents() = std::move(parents);
}

std::unique_ptr<Vst3Plugin>
Vst3Plugin::make(const std::shared_ptr<Vst3Module> &module, const std::string &id, Mono mono)
{
    std::unique_ptr<Vst3Plugin> made(new Vst3Plugin(module, id, mono));
    if (made->m_instances.empty()) {
        return nullptr;
    }
    return made;
}

Vst3Plugin::Vst3Plugin(std::shared_ptr<Vst3Module> module, std::string id, Mono mono)
    : m_module(std::move(module))
    , m_id(std::move(id))
    , m_mono(mono)
    , m_events(std::make_unique<Events>())
{
    // **A class id in either case** is the same plugin; it is kept as this
    // host writes it, which is how presets and states are matched to it.
    if (const std::optional<std::string> canonical = vst3CanonicalId(m_id)) {
        m_id = *canonical;
    } else {
        return;
    }
    if (std::optional<Vst3Halves> made = makeVst3Halves(*m_module, m_id, &m_handler)) {
        m_halves.emplace(std::move(*made));
    } else {
        return;
    }
    Instance first;
    first.component = m_halves->component;
    first.processor = vst3Query<Vst::IAudioProcessor>(first.component.get(), Vst::IAudioProcessor_iid);
    if (!first.processor) {
        m_halves.reset();
        return;
    }
    first.mainIns = vst3MainChannels(first.component.get(), Vst::kInput);
    first.mainOuts = vst3MainChannels(first.component.get(), Vst::kOutput);
    for (const Vst3Module::Class &entry : m_module->classes()) {
        if (entry.id == m_id) {
            m_name = entry.name;
            m_vendor = entry.vendor;
        }
    }
    if (m_vendor.empty()) {
        m_vendor = m_module->vendor();
    }
    if (m_name.empty()) {
        m_name = m_id;
    }

    Vst::IEditController *controller = m_halves->controller.get();
    m_params = vst3Params(controller, &m_ids, &m_curves);
    m_values = std::vector<std::atomic<float>>(m_params.size());
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        m_choices.push_back(choiceValues(m_params[index]));
        const double normalized = controller->getParamNormalized(m_ids[index]);
        m_values[index].store(static_cast<float>(m_curves[index].plainOf(normalized)));
        m_byId.emplace_back(m_ids[index], index);
        dsp::ParamDescriptor descriptor;
        descriptor.id = m_params[index].id;
        descriptor.type = dsp::ParamType::Float;
        descriptor.defaultValue = dsp::ParamValue::ofFloat(m_params[index].defaultValue);
        m_descriptors.push_back(std::move(descriptor));
    }
    std::ranges::sort(m_byId);
    m_told.resize(m_params.size());
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        m_told[index] = m_values[index].load();
    }
    m_changed.assign(m_params.size(), 0);
    m_changedList.assign(m_params.size(), 0);

    // **Every parameter the controller has**, hidden and read-only ones
    // too, for what the sound gives out and what the window moves.
    std::vector<Vst::ParamID> all;
    const int32 count = controller->getParameterCount();
    for (int32 index = 0; index < count; ++index) {
        Vst::ParameterInfo info{};
        if (controller->getParameterInfo(index, info) == kResultOk) {
            all.push_back(info.id);
        }
    }
    std::ranges::sort(all);
    all.erase(std::ranges::unique(all).begin(), all.end());
    m_given = std::vector<Given>(all.size());
    for (std::size_t index = 0; index < all.size(); ++index) {
        m_given[index].id = all[index];
    }
    m_in = std::make_unique<Vst3Changes>(all.size() + m_unlisted.capacity());
    m_out = std::make_unique<Vst3Changes>(all.size());
    m_spareOut = std::make_unique<Vst3Changes>(all.size());

    m_info.typeId = "vst3";
    m_info.category = dsp::BlockCategory::Plugins;
    m_info.inputs = numbered(first.mainIns == 1 ? 2 : first.mainIns, "in_");
    m_info.outputs = numbered(first.mainOuts == 1 ? 2 : first.mainOuts, "out_");
    m_instances.push_back(std::move(first));
}

Vst3Plugin::~Vst3Plugin()
{
    closeUi();
    if (m_tick && loop() != nullptr) {
        loop()->remove(*m_tick);
    }
    for (Instance &instance : m_instances) {
        stop(instance);
    }
    dropSecond();
    m_instances.clear();
    m_halves.reset();
}

void Vst3Plugin::dropSecond()
{
    // The second instance's component alone; the first's goes with its
    // controller.
    while (m_instances.size() > 1) {
        Instance &second = m_instances.back();
        stop(second);
        if (second.point) {
            if (m_halves && m_halves->toComponents) {
                m_halves->toComponents->remove(second.point.get());
            }
            (void)second.point->disconnect(second.relay.get());
        }
        second.point.reset();
        (void)second.component->terminate();
        m_instances.pop_back();
    }
}

void Vst3Plugin::reactivate()
{
    const auto cycle = [this] {
        for (Instance &instance : m_instances) {
            if (!instance.processing) {
                continue;
            }
            (void)instance.processor->setProcessing(false);
            (void)instance.component->setActive(false);
            (void)instance.component->setActive(true);
            (void)instance.processor->setProcessing(true);
        }
    };
    if (events() != nullptr && events()->whileHeld) {
        events()->whileHeld(cycle);
    } else {
        cycle();
    }
}

bool Vst3Plugin::start(Instance &instance, const dsp::PrepareSpec &spec)
{
    Vst::IComponent *component = instance.component.get();
    if (instance.processor->canProcessSampleSize(Vst::kSample32) != kResultTrue) {
        return false;
    }
    // **Only the first main bus each way is heard**; the rest are switched
    // off, and given silence or somewhere to write all the same.
    for (const Vst::BusDirection direction : {Vst::kInput, Vst::kOutput}) {
        const int32 buses = component->getBusCount(Vst::kAudio, direction);
        bool mainFound = false;
        std::size_t &mainBus = direction == Vst::kInput ? instance.mainIn : instance.mainOut;
        mainBus = kNoBus;
        std::vector<Vst::AudioBusBuffers> &list = direction == Vst::kInput ? instance.inputs : instance.outputs;
        std::vector<float *> &channels = direction == Vst::kInput ? instance.inChannels : instance.outChannels;
        list.assign(static_cast<std::size_t>(std::max(buses, 0)), Vst::AudioBusBuffers{});
        std::size_t total = 0;
        for (int32 bus = 0; bus < buses; ++bus) {
            Vst::BusInfo info{};
            (void)component->getBusInfo(Vst::kAudio, direction, bus, info);
            const bool main = !mainFound && info.busType == Vst::kMain;
            mainFound = mainFound || main;
            if (main) {
                mainBus = static_cast<std::size_t>(bus);
            }
            (void)component->activateBus(Vst::kAudio, direction, bus, main);
            list[static_cast<std::size_t>(bus)].numChannels = std::max(info.channelCount, 0);
            total += static_cast<std::size_t>(std::max(info.channelCount, 0));
        }
        channels.assign(std::max<std::size_t>(total, 1), nullptr);
        std::size_t at = 0;
        for (Vst::AudioBusBuffers &buffers : list) {
            buffers.channelBuffers32 = channels.data() + at;
            at += static_cast<std::size_t>(buffers.numChannels);
        }
        // No notes either way.
        for (int32 bus = 0; bus < component->getBusCount(Vst::kEvent, direction); ++bus) {
            (void)component->activateBus(Vst::kEvent, direction, bus, false);
        }
    }
    Vst::ProcessSetup setup{};
    setup.processMode = Vst::kRealtime;
    setup.symbolicSampleSize = Vst::kSample32;
    setup.maxSamplesPerBlock = static_cast<int32>(spec.maxBlockFrames);
    setup.sampleRate = spec.sampleRate;
    if (instance.processor->setupProcessing(setup) != kResultOk || component->setActive(true) != kResultOk) {
        return false;
    }
    // **Started here, on the main thread**, before the audio thread has it:
    // the format lets either thread say so, and this one may allocate.
    (void)instance.processor->setProcessing(true);
    instance.processing = true;
    return true;
}

void Vst3Plugin::stop(Instance &instance)
{
    if (!instance.processing) {
        return;
    }
    // The audio thread has let go of this plugin (the host swaps a block
    // out between cycles).
    (void)instance.processor->setProcessing(false);
    (void)instance.component->setActive(false);
    instance.processing = false;
}

std::expected<void, dsp::BlockError> Vst3Plugin::prepare(const dsp::PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > dsp::kMaxBlockFrames
        || m_instances.empty() || m_active) {
        // Once, like the other formats: a plugin at another rate is
        // another block.
        return std::unexpected(dsp::BlockError::UnsupportedSpec);
    }
    const Instance &first = m_instances.front();
    // **Twice for a mono plugin on a stereo lane, once a side**, unless it
    // is to hear the two sides mixed — or makes more than one side itself.
    if (first.mainIns == 1 && first.mainOuts <= 1 && m_mono == Mono::DualMono) {
        Instance second;
        second.component = makeVst3Component(*m_module, m_id);
        second.processor = second.component
            ? vst3Query<Vst::IAudioProcessor>(second.component.get(), Vst::IAudioProcessor_iid)
            : Vst3Ref<Vst::IAudioProcessor>{};
        if (!second.processor) {
            if (second.component) {
                (void)second.component->terminate();
            }
            return std::unexpected(dsp::BlockError::UnsupportedSpec);
        }
        second.mainIns = first.mainIns;
        second.mainOuts = first.mainOuts;
        // What the first has, the second has: one plugin to the person.
        Vst3Stream state;
        if (first.component->getState(&state) == kResultOk && !state.bytes().empty()) {
            Vst3Stream reading(state.bytes());
            (void)second.component->setState(&reading);
        }
        // **It hears the controller too**, through the same relay as the
        // first; what it says goes nowhere, the first saying it already.
        second.point = vst3Query<Vst::IConnectionPoint>(second.component.get(), Vst::IConnectionPoint_iid);
        if (second.point && m_halves->toComponents) {
            second.relay = std::make_unique<Vst3Relay>();
            (void)second.point->connect(second.relay.get());
            m_halves->toComponents->add(second.point.get());
        } else {
            second.point.reset();
        }
        m_instances.push_back(std::move(second));
    }
    for (Instance &instance : m_instances) {
        if (!start(instance, spec)) {
            for (Instance &started : m_instances) {
                stop(started);
            }
            // A plugin that will not start is made again as a whole.
            dropSecond();
            return std::unexpected(dsp::BlockError::UnsupportedSpec);
        }
    }
    m_active = true;
    m_sampleRate = spec.sampleRate;
    m_silence.assign(spec.maxBlockFrames, 0.0f);
    m_mix.assign(spec.maxBlockFrames, 0.0f);
    m_spare.assign(spec.maxBlockFrames, 0.0f);
    // **Every value, in the first cycle**: what was set before the plugin
    // ran reaches it, and what it has already is no change to it.
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        m_changed[index] = 1;
        m_changedList[index] = index;
    }
    m_changedCount = static_cast<std::uint32_t>(m_params.size());
    refreshLatency();
    return {};
}

void Vst3Plugin::process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING
{
    const std::uint32_t frames = context.frames;
    const auto silenceOutputs = [&context] {
        for (std::uint32_t channel = 0; channel < context.output.channelCount(); ++channel) {
            const std::span<float> out = context.output.channel(channel);
            std::fill(out.begin(), out.end(), 0.0f);
        }
    };
    if (!m_active || frames == 0 || frames > m_silence.size()) {
        silenceOutputs();
        return;
    }
    const auto inputOf = [&context, this](std::uint32_t channel) -> float * {
        const std::span<float> in =
            channel < context.input.channelCount() ? context.input.channel(channel) : std::span<float>{};
        return in.empty() ? m_silence.data() : in.data();
    };
    const auto outputOf = [&context, this](std::uint32_t channel) -> float * {
        const std::span<float> out =
            channel < context.output.channelCount() ? context.output.channel(channel) : std::span<float>{};
        return out.empty() ? m_spare.data() : out.data();
    };
    const bool dual = m_instances.size() == 2;
    const std::uint32_t mainIns = m_instances.front().mainIns;
    const std::uint32_t mainOuts = m_instances.front().mainOuts;
    const bool downmix = mainIns == 1 && !dual;
    if (downmix) {
        const float *left = inputOf(0);
        const float *right = inputOf(1);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            m_mix[frame] = 0.5f * (left[frame] + right[frame]);
        }
    }

    // **The changes since the last cycle**, from its first sample: the
    // engine's and the window's, as 0 to 1, and what the window moved that
    // the block does not show.
    m_in->clear();
    for (std::uint32_t at = 0; at < m_changedCount; ++at) {
        const std::uint32_t index = m_changedList[at];
        m_changed[index] = 0;
        (void)m_in->add(m_ids[index], m_curves[index].normalizedOf(m_values[index].load(std::memory_order_relaxed)));
    }
    m_changedCount = 0;
    Vst3Edit unlisted;
    while (m_unlisted.tryPop(unlisted)) {
        (void)m_in->add(unlisted.id, unlisted.value);
    }

    m_context.state =
        Vst::ProcessContext::kContTimeValid | Vst::ProcessContext::kTempoValid | Vst::ProcessContext::kTimeSigValid;
    m_context.sampleRate = m_sampleRate;
    m_context.projectTimeSamples = m_steadyTime;
    m_context.continousTimeSamples = m_steadyTime;
    m_context.tempo = 120.0;
    m_context.timeSigNumerator = 4;
    m_context.timeSigDenominator = 4;

    for (std::size_t which = 0; which < m_instances.size(); ++which) {
        Instance &instance = m_instances[which];
        // The main bus each way is the block's; any other hears silence
        // and writes where nobody listens.
        std::size_t channel = 0;
        for (std::size_t bus = 0; bus < instance.inputs.size(); ++bus) {
            for (int32 at = 0; at < instance.inputs[bus].numChannels; ++at, ++channel) {
                const auto own = static_cast<std::uint32_t>(at);
                instance.inChannels[channel] = bus != instance.mainIn ? m_silence.data()
                    : dual                                            ? inputOf(static_cast<std::uint32_t>(which))
                    : downmix                                         ? m_mix.data()
                                                                      : inputOf(own);
            }
            instance.inputs[bus].silenceFlags = 0;
        }
        channel = 0;
        for (std::size_t bus = 0; bus < instance.outputs.size(); ++bus) {
            for (int32 at = 0; at < instance.outputs[bus].numChannels; ++at, ++channel) {
                const auto own = static_cast<std::uint32_t>(at);
                instance.outChannels[channel] = bus != instance.mainOut ? m_spare.data()
                    : dual                                              ? outputOf(static_cast<std::uint32_t>(which))
                                                                        : outputOf(own);
            }
            instance.outputs[bus].silenceFlags = 0;
        }
        Vst::ProcessData data;
        data.processMode = Vst::kRealtime;
        data.symbolicSampleSize = Vst::kSample32;
        data.numSamples = static_cast<int32>(frames);
        data.numInputs = static_cast<int32>(instance.inputs.size());
        data.numOutputs = static_cast<int32>(instance.outputs.size());
        data.inputs = instance.inputs.empty() ? nullptr : instance.inputs.data();
        data.outputs = instance.outputs.empty() ? nullptr : instance.outputs.data();
        data.inputParameterChanges = m_in.get();
        Vst3Changes *out = which == 0 ? m_out.get() : m_spareOut.get();
        out->clear();
        data.outputParameterChanges = out;
        data.inputEvents = m_events.get();
        data.outputEvents = nullptr;
        data.processContext = &m_context;
        (void)processOf(instance.processor.get(), data);
    }
    m_steadyTime += frames;

    // **What the sound gave out**, for the controller: a setting it moved
    // is what the setting now is.
    for (std::size_t at = 0; at < m_out->count(); ++at) {
        const std::optional<double> value = m_out->lastAt(at);
        if (!value) {
            continue;
        }
        const Vst::ParamID id = m_out->idAt(at);
        const auto given =
            std::ranges::lower_bound(m_given, id, {}, [](const Given &entry) noexcept { return entry.id; });
        if (given == m_given.end() || given->id != id) {
            continue;
        }
        given->value.store(*value, std::memory_order_relaxed);
        given->fresh.store(true, std::memory_order_release);
        // **Only a value that moved**: one sent and said back is no news.
        if (const std::optional<std::uint32_t> index = indexOf(id)) {
            const Vst3Curve &curve = m_curves[*index];
            if (std::abs(curve.normalizedOf(m_values[*index].load(std::memory_order_relaxed)) - *value) > 1e-6) {
                remember(*index, static_cast<float>(curve.plainOf(*value)));
            }
        }
    }

    // A mono output goes to both sides.
    if (mainOuts == 1 && !dual && context.output.channelCount() > 1) {
        const std::span<float> left = context.output.channel(0);
        const std::span<float> right = context.output.channel(1);
        if (!left.empty() && !right.empty()) {
            std::copy_n(left.begin(), frames, right.begin());
        }
    }
}

void Vst3Plugin::reset() noexcept RB_NONBLOCKING { }

std::optional<std::uint32_t> Vst3Plugin::indexOf(Vst::ParamID id) const noexcept RB_NONBLOCKING
{
    const auto found = std::ranges::lower_bound(
        m_byId, id, {}, [](const std::pair<Vst::ParamID, std::uint32_t> &entry) noexcept { return entry.first; });
    if (found != m_byId.end() && found->first == id) {
        return found->second;
    }
    return std::nullopt;
}

void Vst3Plugin::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    if (index >= m_params.size()) {
        return;
    }
    float actual = value;
    if (m_params[index].kind == plugin::PluginParam::Kind::Choice && !m_choices[index].empty()) {
        // A choice arrives as its position among the plugin's values.
        const std::vector<float> &values = m_choices[index];
        const auto position = static_cast<std::size_t>(std::max(0L, std::lround(value)));
        actual = values[std::min(position, values.size() - 1)];
    }
    remember(index, actual);
    if (m_changed[index] == 0) {
        m_changed[index] = 1;
        m_changedList[m_changedCount++] = index;
    }
}

void Vst3Plugin::remember(std::uint32_t index, float value) noexcept RB_NONBLOCKING
{
    m_values[index].store(value, std::memory_order_relaxed);
}

float Vst3Plugin::blockValue(std::uint32_t index, float value) const noexcept
{
    if (m_params[index].kind != plugin::PluginParam::Kind::Choice || m_choices[index].empty()) {
        return value;
    }
    const std::vector<float> &values = m_choices[index];
    std::size_t nearest = 0;
    for (std::size_t at = 1; at < values.size(); ++at) {
        if (std::abs(values[at] - value) < std::abs(values[nearest] - value)) {
            nearest = at;
        }
    }
    return static_cast<float>(nearest);
}

std::expected<void, dsp::BlockError> Vst3Plugin::setParam(std::string_view id, const dsp::ParamValue &value)
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            // The value itself, as a state holds it; it reaches the plugin
            // in its first cycle. Once it runs, a change comes through
            // applyParam().
            remember(index, static_cast<float>(value.asNumber().value_or(0.0)));
            return {};
        }
    }
    return std::unexpected(dsp::BlockError::UnknownParam);
}

dsp::ParamValue Vst3Plugin::param(std::string_view id) const
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            return dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed));
        }
    }
    return {};
}

dsp::ParamValueMap Vst3Plugin::saveState() const
{
    dsp::ParamValueMap state;
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        state.emplace(m_params[index].id, dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed)));
    }
    return state;
}

std::expected<void, dsp::BlockError> Vst3Plugin::loadState(const dsp::ParamValueMap &state)
{
    for (const auto &[id, value] : state) {
        (void)setParam(id, value);
    }
    return {};
}

Vst3Preset Vst3Plugin::capture() const
{
    Vst3Preset preset;
    preset.classId = m_id;
    Vst3Stream component;
    if (m_instances.front().component->getState(&component) == kResultOk) {
        preset.component = component.bytes();
    }
    Vst3Stream controller;
    if (m_halves->controller->getState(&controller) == kResultOk) {
        preset.controller = controller.bytes();
    }
    return preset;
}

bool Vst3Plugin::apply(const Vst3Preset &preset)
{
    // **An empty state is none**: some plugins cannot be given one, and
    // keep what they have.
    if (!preset.component.empty()) {
        // Every instance: a mono plugin run twice is one plugin to the
        // person.
        for (const Instance &instance : m_instances) {
            Vst3Stream reading(preset.component);
            if (instance.component->setState(&reading) != kResultOk) {
                return false;
            }
        }
        Vst3Stream shown(preset.component);
        (void)m_halves->controller->setComponentState(&shown);
    }
    if (preset.controller && !preset.controller->empty()) {
        Vst3Stream own(*preset.controller);
        (void)m_halves->controller->setState(&own);
    }
    readValues();
    return true;
}

std::string Vst3Plugin::stateText() const
{
    return toBase64(writeVst3Preset(capture()));
}

bool Vst3Plugin::loadStateText(const std::string &text)
{
    const std::optional<std::string> bytes = fromBase64(text);
    const std::optional<Vst3Preset> preset = bytes ? readVst3Preset(*bytes) : std::nullopt;
    if (!preset || preset->classId != m_id) {
        return false;
    }
    return apply(*preset);
}

void Vst3Plugin::readValues()
{
    Vst::IEditController *controller = m_halves->controller.get();
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        const auto value = static_cast<float>(m_curves[index].plainOf(controller->getParamNormalized(m_ids[index])));
        remember(index, value);
        m_told[index] = value;
    }
}

void Vst3Plugin::refreshLatency() noexcept
{
    if (m_active) {
        m_info.latencySamples = m_instances.front().processor->getLatencySamples();
    }
}

bool Vst3Plugin::idle()
{
    if (m_latencyChanged.load() && m_active) {
        reactivate();
        refreshLatency();
        // What it said again as it was activated is this same latency.
        m_latencyChanged.store(false);
    }
    return m_restart.exchange(false);
}

// --- what a person does in the window ---

void Vst3Plugin::gesture(Vst::ParamID id, bool begin)
{
    const std::optional<std::uint32_t> index = indexOf(id);
    if (index && events() != nullptr && events()->gesture) {
        events()->gesture(*index, begin);
    }
}

void Vst3Plugin::edited(Vst::ParamID id, double normalized)
{
    const std::optional<std::uint32_t> index = indexOf(id);
    if (!index) {
        // **Not one of the block's settings**, but the sound needs it.
        (void)m_unlisted.tryPush(Vst3Edit{id, normalized});
        return;
    }
    // **The plugin's first, and never sent back**: the controller has it
    // already, and the sound hears it through the host's queue.
    const auto value = static_cast<float>(m_curves[*index].plainOf(normalized));
    remember(*index, value);
    m_told[*index] = value;
    if (events() == nullptr) {
        return;
    }
    if (events()->changed) {
        events()->changed(*index, value);
    }
    if (events()->toAudio) {
        events()->toAudio(*index, blockValue(*index, value));
    }
}

void Vst3Plugin::onAttached()
{
    // What the plugin said while it was being made is not a person's
    // doing, nor a reason to make it again.
    (void)m_rescan.exchange(false);
    (void)m_restart.exchange(false);
    (void)m_latencyChanged.exchange(false);
    if (!m_tick && loop() != nullptr) {
        m_tick = loop()->addTimer(std::chrono::milliseconds{33}, [this] { editorTick(); });
    }
}

void Vst3Plugin::editorTick()
{
    if (!m_halves) {
        return;
    }
    Vst::IEditController *controller = m_halves->controller.get();
    // **What the sound gave out**, a meter or a setting it moved, told to
    // the controller.
    for (Given &given : m_given) {
        if (given.fresh.exchange(false, std::memory_order_acquire) && !indexOf(given.id)) {
            (void)controller->setParamNormalized(given.id, given.value.load(std::memory_order_relaxed));
        }
    }
    // **What moved elsewhere**, the panel or the sound, told to the
    // controller and so to its window.
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        const float value = m_values[index].load(std::memory_order_relaxed);
        if (value != m_told[index]) {
            m_told[index] = value;
            (void)controller->setParamNormalized(m_ids[index], m_curves[index].normalizedOf(value));
        }
    }
    // **Values the controller changed itself**, a preset chosen in its own
    // window: read, and given to the sound, which has not heard of them.
    if (m_rescan.exchange(false)) {
        std::vector<float> before(m_params.size());
        for (std::uint32_t index = 0; index < m_params.size(); ++index) {
            before[index] = m_values[index].load(std::memory_order_relaxed);
        }
        readValues();
        if (events() != nullptr) {
            for (std::uint32_t index = 0; index < m_params.size(); ++index) {
                const float now = m_values[index].load(std::memory_order_relaxed);
                if (now != before[index] && events()->toAudio) {
                    events()->toAudio(index, blockValue(index, now));
                }
            }
            if (events()->valuesChanged) {
                events()->valuesChanged(uiOpen());
            }
        }
    }
}

// --- the plugin's own window ---

bool Vst3Plugin::hasUi() const
{
    return m_halves.has_value() && (viewParents() || haveDisplay());
}

Editable::Opened Vst3Plugin::openUi()
{
    if (m_view) {
        return Opened::Open;
    }
    if (!hasUi() || loop() == nullptr) {
        return Opened::Unavailable;
    }
    Vst3Ref<IPlugView> view(m_halves->controller->createView(Vst::ViewType::kEditor));
    if (!view || view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue) {
        return Opened::Unavailable;
    }
    (void)view->setFrame(&m_frame);
    ViewRect rect;
    (void)view->getSize(&rect);
    const std::string key = "vst3 " + m_id;
    const auto closed = [this](bool failed) {
        closeUi();
        if (events() != nullptr && events()->closed) {
            events()->closed(failed ? "failed" : "closed");
        }
    };
    const auto resized = [this](int width, int height) {
        if (!m_view || m_view->canResize() != kResultTrue) {
            return;
        }
        ViewRect wanted(0, 0, width, height);
        (void)m_view->checkSizeConstraint(&wanted);
        (void)m_view->onSize(&wanted);
    };
    if (const Vst3ViewParents &parents = viewParents()) {
        m_window = parents(*loop(), m_name, key, rect.getWidth(), rect.getHeight(), closed, resized);
    } else if (std::unique_ptr<X11Window> window =
                   X11Window::open(*loop(), m_name, key, rect.getWidth(), rect.getHeight(), closed, resized)) {
        m_window = std::make_unique<X11Parent>(std::move(window));
    }
    if (!m_window) {
        (void)view->setFrame(nullptr);
        return Opened::Unavailable;
    }
    m_view = std::move(view);
    const bool resizable = m_view->canResize() == kResultTrue;
    m_window->setResizable(resizable);
    std::optional<ViewRect> kept;
    if (const std::optional<WindowPlace> place = placeOf(key); place && resizable) {
        // Its size from last time, as near as the plugin takes it.
        ViewRect wanted(0, 0, place->width, place->height);
        (void)m_view->checkSizeConstraint(&wanted);
        kept = wanted;
        m_window->resize(wanted.getWidth(), wanted.getHeight());
    } else if (rect.getWidth() > 0 && rect.getHeight() > 0) {
        m_window->resize(rect.getWidth(), rect.getHeight());
    }
    if (m_view->attached(reinterpret_cast<void *>(m_window->id()), kPlatformTypeX11EmbedWindowID) != kResultOk) {
        // **Never attached, so never removed**: only let go of.
        (void)m_view->setFrame(nullptr);
        m_view.reset();
        m_frame.clear();
        m_window.reset();
        return Opened::Failed;
    }
    if (kept) {
        (void)m_view->onSize(&*kept);
    }
    m_window->show();
    return Opened::Open;
}

void Vst3Plugin::closeUi()
{
    if (m_view) {
        (void)m_view->removed();
        (void)m_view->setFrame(nullptr);
        m_view.reset();
    }
    // What the view left on the loop, then the window it was in.
    m_frame.clear();
    m_window.reset();
}

// --- presets ---

std::vector<plugin::PresetEntry> Vst3Plugin::presets()
{
    std::vector<plugin::PresetEntry> listed;
    for (const std::string &folder : vst3PresetFolders(m_vendor, m_name)) {
        std::error_code error;
        for (fs::recursive_directory_iterator walk(folder, fs::directory_options::skip_permission_denied, error);
             !error && walk != fs::recursive_directory_iterator();
             walk.increment(error)) {
            if (!walk->is_regular_file(error) || walk->path().extension() != ".vstpreset") {
                continue;
            }
            // **Only this plugin's**: a preset says whose it is, in its
            // header.
            if (vst3PresetClassOf(walk->path().string()) == m_id) {
                listed.push_back(plugin::PresetEntry{walk->path().string(), walk->path().stem().string()});
            }
        }
    }
    std::ranges::sort(listed, [](const plugin::PresetEntry &left, const plugin::PresetEntry &right) {
        return left.name != right.name ? left.name < right.name : left.id < right.id;
    });
    return listed;
}

bool Vst3Plugin::loadPreset(const std::string &id)
{
    // **Only a file where this plugin's presets are**: an id is a path,
    // and one from elsewhere is not a preset of the plugin's.
    const std::vector<std::string> folders = vst3PresetFolders(m_vendor, m_name);
    if (std::ranges::none_of(folders, [&id](const std::string &folder) { return isInside(id, folder); })) {
        return false;
    }
    const std::optional<std::string> bytes = readFile(id);
    const std::optional<Vst3Preset> preset = bytes ? readVst3Preset(*bytes) : std::nullopt;
    if (!preset || preset->classId != m_id || !m_active) {
        return false;
    }
    // **With the sound kept away from the plugin** while its state is put
    // in, as for an LV2 plugin.
    bool applied = false;
    if (events() != nullptr && events()->whileHeld) {
        events()->whileHeld([&] { applied = apply(*preset); });
    } else {
        applied = apply(*preset);
    }
    if (!applied) {
        return false;
    }
    if (events() != nullptr) {
        if (events()->valuesChanged) {
            events()->valuesChanged(true);
        }
        if (events()->presetShown) {
            events()->presetShown(id);
        }
    }
    return true;
}

std::optional<std::string> Vst3Plugin::savePreset(const std::string &name)
{
    const std::vector<std::string> folders = vst3PresetFolders(m_vendor, m_name);
    if (folders.empty() || name.empty() || !m_halves) {
        return std::nullopt;
    }
    std::error_code error;
    fs::create_directories(folders.front(), error);
    const std::string fileName = vst3FileName(name);
    const fs::path file = fs::path{folders.front()} / (fileName + ".vstpreset");
    // **Never over another preset**: saving onto the same name replaces it;
    // a file of another plugin's, or of a name that made the same file
    // name, is refused.
    if (fs::exists(file, error)) {
        const bool same = fileName == name && vst3PresetClassOf(file.string()) == m_id;
        if (!same) {
            std::fprintf(stderr, "a preset is already called %s\n", file.string().c_str());
            return std::nullopt;
        }
    }
    // **Written whole, then put in place**: a preset half written is never
    // there to be read.
    const fs::path partial = fs::path{folders.front()} / ("." + fileName + ".vstpreset." + std::to_string(::getpid()));
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        const std::string bytes = writeVst3Preset(capture());
        if (!out || !out.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !out.flush()) {
            std::fprintf(stderr, "could not write the preset %s\n", file.string().c_str());
            fs::remove(partial, error);
            return std::nullopt;
        }
    }
    fs::rename(partial, file, error);
    if (error) {
        fs::remove(partial, error);
        return std::nullopt;
    }
    return file.string();
}

} // namespace rb::pluginhost

#pragma GCC diagnostic pop

// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The tests' VST3 plugin, written against the format's interface headers
// alone: a gain, parameter 1, from 0 to 4 (a quarter of its 0 to 1 is 1).
// Two of them, a stereo one with a mono side chain and a mono one, made by
// one component class each, and a controller class they share, joined
// through connection points. Besides the gain, its settings are:
//
//   2 "Colour"  three steps with words: Clean, Warm, Hot
//   3           a hidden one
//   4 "Level"   read-only: what the sound last gave out, as a meter
//
// The component keeps the gain and the colour; the controller keeps a
// "zoom" of its own, which its view sets to 2. Its component greets the
// controller through a message the host makes, once they are joined.
//
// **Its view is a stand-in that needs no display**: it says it can be put
// in an X11 window, and once it is, it watches a descriptor of its own and
// asks for a timer through the run loop the window offers, as a real
// view's toolkit would; one object is the view, the descriptor's handler
// and the timer's. On its timer it behaves as a person: it asks for a
// larger window, takes hold of the gain, moves it to a quarter, rests for
// longer than a guessed gesture lasts, moves it to 0.3 and lets go, all
// through the controller's handler. It writes down, to the file
// RB_TEST_VST3_LOG names:
//
//   attached | removed            what the host did with the view
//   size <w>x<h>                  the size the host gave it
//   fd                            its descriptor was read
//   gain <value>                  the gain the host last set, as it heard it
//   hello <n>                     the component's greeting arrived
//   level                         the controller heard the meter, once
//   zoom <n>                      the controller's own state came back
//   component heard gain          a component heard the controller say the
//                                 gain changed (each instance says so)
//   cycle                         the controller heard the component speak
//                                 from its first process call
//   given an empty state          a half was given a state of no bytes
//   refused to attach             the view would not be attached
//
// Its latency is 16 samples a step of its colour, taken on when it is
// activated; a new colour has the controller ask for that. What the
// environment asks of it, for the tests that need it:
//
//   RB_TEST_VST3_TALK             speak from the first process call
//   RB_TEST_VST3_EMPTY_STATE      keep a component state of no bytes
//   RB_TEST_VST3_REFUSE_SETUP     refuse to be set up for processing
//   RB_TEST_VST3_REFUSE_ATTACH    refuse to have its view attached
//
// The stereo plugin's side chain comes before its main input, which the
// format allows, so that a host has to find the main bus.

#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/gui/iplugview.h>
#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivsthostapplication.h>
#include <pluginterfaces/vst/ivstmessage.h>
#include <pluginterfaces/vst/ivstparameterchanges.h>
#include <pluginterfaces/vst/vstspeaker.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// The format's interfaces have no virtual destructors; nothing is deleted
// through one.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

using namespace Steinberg;

namespace
{

constexpr Vst::ParamID kGain = 1;
constexpr Vst::ParamID kColour = 2;
constexpr Vst::ParamID kHidden = 3;
constexpr Vst::ParamID kLevel = 4;
/// Timer calls between the two moves, at ten milliseconds each: more than
/// the half second a guessed gesture ends after.
constexpr int kHeldFor = 70;

const TUID kStereoId = INLINE_UID(0x52425653, 0x54334741, 0x494E5354, 0x4552454F);
const TUID kMonoId = INLINE_UID(0x52425653, 0x54334741, 0x494E4D4F, 0x4E4F2020);
const TUID kControllerId = INLINE_UID(0x52425653, 0x54334354, 0x524C4C45, 0x52202020);

[[nodiscard]] bool same(const TUID left, const TUID right)
{
    return FUnknownPrivate::iidEqual(left, right);
}

void note(const std::string &line)
{
    const char *log = std::getenv("RB_TEST_VST3_LOG");
    if (log == nullptr || *log == '\0') {
        return;
    }
    if (std::FILE *file = std::fopen(log, "a"); file != nullptr) {
        std::fprintf(file, "%s\n", line.c_str());
        std::fclose(file);
    }
}

void setText(Vst::String128 into, const char *text)
{
    std::size_t at = 0;
    for (; text[at] != '\0' && at + 1 < 128; ++at) {
        into[at] = static_cast<char16>(text[at]);
    }
    into[at] = 0;
}

/// Counts its references and goes with the last.
template <typename Self>
class Counted
{
public:
    uint32 take()
    {
        return ++m_count;
    }
    uint32 drop(Self *self)
    {
        const uint32 left = --m_count;
        if (left == 0) {
            delete self;
        }
        return left;
    }

private:
    std::atomic<uint32> m_count{1};
};

#define COUNTED(Self)                                                                                                  \
    uint32 PLUGIN_API addRef() override                                                                                \
    {                                                                                                                  \
        return m_counted.take();                                                                                       \
    }                                                                                                                  \
    uint32 PLUGIN_API release() override                                                                               \
    {                                                                                                                  \
        return m_counted.drop(this);                                                                                   \
    }                                                                                                                  \
    Counted<Self> m_counted;

/// Whether \a stream has nothing in it.
bool isEmpty(IBStream *stream)
{
    int64 size = 0;
    if (stream == nullptr || stream->seek(0, IBStream::kIBSeekEnd, &size) != kResultOk) {
        return true;
    }
    (void)stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return size == 0;
}

template <typename T>
bool readAll(IBStream *stream, T &value)
{
    int32 read = 0;
    return stream != nullptr && stream->read(&value, sizeof(T), &read) == kResultOk && read == sizeof(T);
}

template <typename T>
bool writeAll(IBStream *stream, T value)
{
    int32 written = 0;
    return stream != nullptr && stream->write(&value, sizeof(T), &written) == kResultOk && written == sizeof(T);
}

// --- the component: the sound ---

class Component final : public Vst::IComponent, public Vst::IAudioProcessor, public Vst::IConnectionPoint
{
public:
    explicit Component(bool mono)
        : m_mono(mono)
    {
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
    {
        if (same(iid, FUnknown_iid) || same(iid, IPluginBase_iid) || same(iid, Vst::IComponent_iid)) {
            *object = static_cast<Vst::IComponent *>(this);
        } else if (same(iid, Vst::IAudioProcessor_iid)) {
            *object = static_cast<Vst::IAudioProcessor *>(this);
        } else if (same(iid, Vst::IConnectionPoint_iid)) {
            *object = static_cast<Vst::IConnectionPoint *>(this);
        } else {
            *object = nullptr;
            return kNoInterface;
        }
        addRef();
        return kResultOk;
    }
    COUNTED(Component)

    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown *context) override
    {
        void *host = nullptr;
        if (context == nullptr || context->queryInterface(Vst::IHostApplication_iid, &host) != kResultOk) {
            return kResultFalse;
        }
        m_host = static_cast<Vst::IHostApplication *>(host);
        m_talks = flag("RB_TEST_VST3_TALK");
        m_emptyState = flag("RB_TEST_VST3_EMPTY_STATE");
        m_refusesSetup = flag("RB_TEST_VST3_REFUSE_SETUP");
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override
    {
        if (m_host != nullptr) {
            m_host->release();
            m_host = nullptr;
        }
        return kResultOk;
    }

    // IComponent
    tresult PLUGIN_API getControllerClassId(TUID classId) override
    {
        std::memcpy(classId, kControllerId, sizeof(TUID));
        return kResultOk;
    }
    tresult PLUGIN_API setIoMode(Vst::IoMode) override
    {
        return kResultOk;
    }
    int32 PLUGIN_API getBusCount(Vst::MediaType type, Vst::BusDirection direction) override
    {
        if (type != Vst::kAudio) {
            return 0;
        }
        // The stereo one has a side chain as well, before its main input.
        return direction == Vst::kInput && !m_mono ? 2 : 1;
    }
    /// Which bus is the main one: after the side chain, for the stereo
    /// one, which the format allows a plugin though few do it.
    [[nodiscard]] int32 mainBus(Vst::BusDirection direction) const
    {
        return direction == Vst::kInput && !m_mono ? 1 : 0;
    }
    tresult PLUGIN_API getBusInfo(Vst::MediaType type,
                                  Vst::BusDirection direction,
                                  int32 index,
                                  Vst::BusInfo &bus) override
    {
        if (type != Vst::kAudio || index < 0 || index >= getBusCount(type, direction)) {
            return kInvalidArgument;
        }
        const bool main = index == mainBus(direction);
        bus.mediaType = Vst::kAudio;
        bus.direction = direction;
        bus.channelCount = main ? (m_mono ? 1 : 2) : 1;
        setText(bus.name, main ? "Main" : "Side chain");
        bus.busType = main ? Vst::kMain : Vst::kAux;
        bus.flags = main ? static_cast<uint32>(Vst::BusInfo::kDefaultActive) : 0U;
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(Vst::RoutingInfo &, Vst::RoutingInfo &) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API activateBus(Vst::MediaType, Vst::BusDirection, int32, TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override
    {
        // **Its latency is what its colour was when it was activated**: a
        // host that does not activate it again never hears a new one.
        if (state != 0) {
            m_latency = 16U * static_cast<uint32>(m_colour);
        }
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *state) override
    {
        if (isEmpty(state)) {
            // As some plugins would rather crash on it.
            note("given an empty state");
            return kResultFalse;
        }
        float gain = 0.0f;
        std::int32_t colour = 0;
        if (!readAll(state, gain) || !readAll(state, colour)) {
            return kResultFalse;
        }
        m_gain.store(gain);
        m_colour = colour;
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *state) override
    {
        if (m_emptyState) {
            return kResultOk;
        }
        return writeAll(state, m_gain.load()) && writeAll(state, m_colour) ? kResultOk : kResultFalse;
    }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(Vst::SpeakerArrangement *, int32, Vst::SpeakerArrangement *, int32) override
    {
        return kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(Vst::BusDirection direction,
                                         int32 index,
                                         Vst::SpeakerArrangement &arrangement) override
    {
        arrangement = index == mainBus(direction) && !m_mono ? Vst::SpeakerArr::kStereo : Vst::SpeakerArr::kMono;
        return index >= 0 && index < getBusCount(Vst::kAudio, direction) ? kResultOk : kInvalidArgument;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == Vst::kSample32 ? kResultTrue : kResultFalse;
    }
    uint32 PLUGIN_API getLatencySamples() override
    {
        return m_latency;
    }
    tresult PLUGIN_API setupProcessing(Vst::ProcessSetup &) override
    {
        return m_refusesSetup ? kResultFalse : kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API process(Vst::ProcessData &data) override
    {
        // **A word to the controller from inside the first process call**,
        // as some plugins send theirs: a host must not let the controller
        // hear it on the audio thread.
        if (m_talks && !m_talked && m_peer != nullptr) {
            m_talked = true;
            say(m_peer, "cycle");
        }
        if (data.inputParameterChanges != nullptr) {
            for (int32 at = 0; at < data.inputParameterChanges->getParameterCount(); ++at) {
                Vst::IParamValueQueue *queue = data.inputParameterChanges->getParameterData(at);
                int32 offset = 0;
                Vst::ParamValue value = 0.0;
                if (queue == nullptr || queue->getPoint(queue->getPointCount() - 1, offset, value) != kResultOk) {
                    continue;
                }
                if (queue->getParameterId() == kGain) {
                    m_gain.store(static_cast<float>(value));
                } else if (queue->getParameterId() == kColour) {
                    m_colour = std::min<std::int32_t>(2, static_cast<std::int32_t>(value * 3.0));
                }
            }
        }
        if (data.numInputs <= mainBus(Vst::kInput) || data.numOutputs <= mainBus(Vst::kOutput)) {
            return kResultOk;
        }
        const float gain = 4.0f * m_gain.load();
        float peak = 0.0f;
        const Vst::AudioBusBuffers &in = data.inputs[mainBus(Vst::kInput)];
        const Vst::AudioBusBuffers &out = data.outputs[mainBus(Vst::kOutput)];
        for (int32 channel = 0; channel < out.numChannels; ++channel) {
            const float *from = channel < in.numChannels ? in.channelBuffers32[channel] : nullptr;
            float *to = out.channelBuffers32[channel];
            for (int32 frame = 0; frame < data.numSamples; ++frame) {
                to[frame] = from != nullptr ? from[frame] * gain : 0.0f;
                peak = std::max(peak, std::abs(to[frame]));
            }
        }
        // The meter, for the controller.
        if (data.outputParameterChanges != nullptr) {
            int32 index = 0;
            if (Vst::IParamValueQueue *queue = data.outputParameterChanges->addParameterData(kLevel, index)) {
                int32 point = 0;
                (void)queue->addPoint(0, std::min(1.0f, peak / 4.0f), point);
            }
        }
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override
    {
        return 0;
    }

    // IConnectionPoint
    tresult PLUGIN_API connect(Vst::IConnectionPoint *other) override
    {
        if (other == nullptr || m_host == nullptr) {
            return kInvalidArgument;
        }
        // **A greeting, in a message the host makes**, as the SDK's
        // components send their controllers what they need.
        m_peer = other;
        say(other, "hello");
        return kResultOk;
    }
    tresult PLUGIN_API disconnect(Vst::IConnectionPoint *) override
    {
        m_peer = nullptr;
        return kResultOk;
    }
    tresult PLUGIN_API notify(Vst::IMessage *message) override
    {
        if (message != nullptr && std::strcmp(message->getMessageID(), "gain") == 0) {
            note("component heard gain");
        }
        return kResultOk;
    }

private:
    [[nodiscard]] static bool flag(const char *name)
    {
        const char *value = std::getenv(name);
        return value != nullptr && *value != '\0';
    }
    void say(Vst::IConnectionPoint *to, const char *id)
    {
        void *made = nullptr;
        TUID messageId;
        std::memcpy(messageId, Vst::IMessage_iid, sizeof(TUID));
        if (m_host->createInstance(messageId, messageId, &made) == kResultOk && made != nullptr) {
            auto *message = static_cast<Vst::IMessage *>(made);
            message->setMessageID(id);
            (void)message->getAttributes()->setInt("n", 7);
            (void)to->notify(message);
            message->release();
        }
    }

    bool m_mono = false;
    bool m_talks = false;
    bool m_talked = false;
    bool m_emptyState = false;
    bool m_refusesSetup = false;
    Vst::IConnectionPoint *m_peer = nullptr;
    uint32 m_latency = 0;
    Vst::IHostApplication *m_host = nullptr;
    std::atomic<float> m_gain{0.25f};
    std::int32_t m_colour = 0;
};

// --- the controller and its view ---

class Controller;

class View final : public IPlugView, public Linux::IEventHandler, public Linux::ITimerHandler
{
public:
    explicit View(Controller &controller);
    ~View();

    tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
    {
        if (same(iid, FUnknown_iid) || same(iid, IPlugView_iid)) {
            *object = static_cast<IPlugView *>(this);
        } else if (same(iid, Linux::IEventHandler_iid)) {
            *object = static_cast<Linux::IEventHandler *>(this);
        } else if (same(iid, Linux::ITimerHandler_iid)) {
            *object = static_cast<Linux::ITimerHandler *>(this);
        } else {
            *object = nullptr;
            return kNoInterface;
        }
        addRef();
        return kResultOk;
    }
    COUNTED(View)

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override
    {
        return std::strcmp(type, kPlatformTypeX11EmbedWindowID) == 0 ? kResultTrue : kResultFalse;
    }
    tresult PLUGIN_API attached(void *parent, FIDString type) override
    {
        if (parent == nullptr || isPlatformTypeSupported(type) != kResultTrue || m_frame == nullptr) {
            return kResultFalse;
        }
        if (const char *refuse = std::getenv("RB_TEST_VST3_REFUSE_ATTACH"); refuse != nullptr && *refuse != '\0') {
            note("refused to attach");
            return kResultFalse;
        }
        void *loop = nullptr;
        if (m_frame->queryInterface(Linux::IRunLoop_iid, &loop) != kResultOk || loop == nullptr) {
            return kResultFalse;
        }
        m_loop = static_cast<Linux::IRunLoop *>(loop);
        m_fd = ::eventfd(1, EFD_NONBLOCK | EFD_CLOEXEC);
        if (m_fd < 0 || m_loop->registerEventHandler(this, m_fd) != kResultOk
            || m_loop->registerTimer(this, 10) != kResultOk) {
            return kResultFalse;
        }
        note("attached");
        return kResultOk;
    }
    tresult PLUGIN_API removed() override;
    tresult PLUGIN_API onWheel(float) override
    {
        return kResultFalse;
    }
    tresult PLUGIN_API onKeyDown(char16, int16, int16) override
    {
        return kResultFalse;
    }
    tresult PLUGIN_API onKeyUp(char16, int16, int16) override
    {
        return kResultFalse;
    }
    tresult PLUGIN_API getSize(ViewRect *size) override
    {
        *size = m_size;
        return kResultOk;
    }
    tresult PLUGIN_API onSize(ViewRect *size) override
    {
        m_size = *size;
        note("size " + std::to_string(size->getWidth()) + "x" + std::to_string(size->getHeight()));
        return kResultOk;
    }
    tresult PLUGIN_API onFocus(TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setFrame(IPlugFrame *frame) override
    {
        m_frame = frame;
        return kResultOk;
    }
    tresult PLUGIN_API canResize() override
    {
        return kResultTrue;
    }
    tresult PLUGIN_API checkSizeConstraint(ViewRect *) override
    {
        return kResultOk;
    }

    void PLUGIN_API onFDIsSet(Linux::FileDescriptor fd) override
    {
        std::uint64_t count = 0;
        if (::read(fd, &count, sizeof(count)) == sizeof(count)) {
            note("fd");
        }
    }
    void PLUGIN_API onTimer() override;

private:
    Controller &m_controller;
    IPlugFrame *m_frame = nullptr;
    Linux::IRunLoop *m_loop = nullptr;
    ViewRect m_size{0, 0, 300, 150};
    int m_fd = -1;
    int m_ticks = 0;
    bool m_holdsController = true;
};

class Controller final : public Vst::IEditController, public Vst::IConnectionPoint
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
    {
        if (same(iid, FUnknown_iid) || same(iid, IPluginBase_iid) || same(iid, Vst::IEditController_iid)) {
            *object = static_cast<Vst::IEditController *>(this);
        } else if (same(iid, Vst::IConnectionPoint_iid)) {
            *object = static_cast<Vst::IConnectionPoint *>(this);
        } else {
            *object = nullptr;
            return kNoInterface;
        }
        addRef();
        return kResultOk;
    }
    COUNTED(Controller)

    tresult PLUGIN_API initialize(FUnknown *context) override
    {
        void *host = nullptr;
        if (context == nullptr || context->queryInterface(Vst::IHostApplication_iid, &host) != kResultOk) {
            return kResultFalse;
        }
        m_host = static_cast<Vst::IHostApplication *>(host);
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override
    {
        if (m_host != nullptr) {
            m_host->release();
            m_host = nullptr;
        }
        return kResultOk;
    }

    tresult PLUGIN_API setComponentState(IBStream *state) override
    {
        if (isEmpty(state)) {
            note("given an empty state");
            return kResultFalse;
        }
        float gain = 0.0f;
        std::int32_t colour = 0;
        if (!readAll(state, gain) || !readAll(state, colour)) {
            return kResultFalse;
        }
        m_gain = gain;
        m_colour = colour;
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *state) override
    {
        if (isEmpty(state)) {
            note("given an empty state");
            return kResultFalse;
        }
        std::int32_t zoom = 0;
        if (!readAll(state, zoom)) {
            return kResultFalse;
        }
        m_zoom = zoom;
        note("zoom " + std::to_string(zoom));
        return kResultOk;
    }
    tresult PLUGIN_API getState(IBStream *state) override
    {
        return writeAll(state, m_zoom) ? kResultOk : kResultFalse;
    }
    int32 PLUGIN_API getParameterCount() override
    {
        return 4;
    }
    tresult PLUGIN_API getParameterInfo(int32 index, Vst::ParameterInfo &info) override
    {
        info = Vst::ParameterInfo{};
        switch (index) {
        case 0:
            info.id = kGain;
            setText(info.title, "Gain");
            info.defaultNormalizedValue = 0.25;
            info.flags = Vst::ParameterInfo::kCanAutomate;
            return kResultOk;
        case 1:
            info.id = kColour;
            setText(info.title, "Colour");
            info.stepCount = 2;
            info.flags = Vst::ParameterInfo::kCanAutomate | Vst::ParameterInfo::kIsList;
            return kResultOk;
        case 2:
            info.id = kHidden;
            setText(info.title, "Hidden");
            info.flags = Vst::ParameterInfo::kIsHidden;
            return kResultOk;
        case 3:
            info.id = kLevel;
            setText(info.title, "Level");
            info.flags = Vst::ParameterInfo::kIsReadOnly;
            return kResultOk;
        default:
            return kInvalidArgument;
        }
    }
    tresult PLUGIN_API getParamStringByValue(Vst::ParamID id, Vst::ParamValue value, Vst::String128 text) override
    {
        if (id == kColour) {
            static constexpr const char *kWords[] = {"Clean", "Warm", "Hot"};
            setText(text, kWords[std::min(2, static_cast<int>(value * 3.0))]);
            return kResultOk;
        }
        const std::string number = std::to_string(normalizedParamToPlain(id, value));
        setText(text, number.c_str());
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(Vst::ParamID, Vst::TChar *, Vst::ParamValue &) override
    {
        return kNotImplemented;
    }
    Vst::ParamValue PLUGIN_API normalizedParamToPlain(Vst::ParamID id, Vst::ParamValue value) override
    {
        if (id == kGain) {
            return 4.0 * value;
        }
        if (id == kColour) {
            return std::min(2.0, std::floor(value * 3.0));
        }
        return value;
    }
    Vst::ParamValue PLUGIN_API plainParamToNormalized(Vst::ParamID id, Vst::ParamValue value) override
    {
        if (id == kGain) {
            return value / 4.0;
        }
        if (id == kColour) {
            return value / 2.0;
        }
        return value;
    }
    Vst::ParamValue PLUGIN_API getParamNormalized(Vst::ParamID id) override
    {
        if (id == kGain) {
            return m_gain;
        }
        if (id == kColour) {
            return m_colour / 2.0;
        }
        return 0.0;
    }
    tresult PLUGIN_API setParamNormalized(Vst::ParamID id, Vst::ParamValue value) override
    {
        if (id == kGain) {
            m_gain = value;
            note("gain " + std::to_string(normalizedParamToPlain(id, value)).substr(0, 4));
            // Every component hears of it: a mono plugin's second too.
            tell("gain");
        } else if (id == kColour) {
            const auto colour = static_cast<std::int32_t>(normalizedParamToPlain(id, value));
            // A new colour is a new latency, which the host takes on by
            // activating the plugin again.
            if (colour != m_colour && m_handler != nullptr) {
                m_colour = colour;
                (void)m_handler->restartComponent(Vst::kLatencyChanged);
            }
            m_colour = colour;
        } else if (id == kLevel && !m_heardLevel) {
            m_heardLevel = true;
            note("level");
        }
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(Vst::IComponentHandler *handler) override
    {
        m_handler = handler;
        return kResultOk;
    }
    IPlugView *PLUGIN_API createView(FIDString name) override
    {
        if (name == nullptr || std::strcmp(name, Vst::ViewType::kEditor) != 0) {
            return nullptr;
        }
        return new View(*this);
    }

    tresult PLUGIN_API connect(Vst::IConnectionPoint *other) override
    {
        m_peer = other;
        return kResultOk;
    }
    tresult PLUGIN_API disconnect(Vst::IConnectionPoint *) override
    {
        m_peer = nullptr;
        return kResultOk;
    }
    tresult PLUGIN_API notify(Vst::IMessage *message) override
    {
        if (message != nullptr && std::strcmp(message->getMessageID(), "cycle") == 0) {
            note("cycle");
        }
        int64 n = 0;
        if (message != nullptr && std::strcmp(message->getMessageID(), "hello") == 0
            && message->getAttributes()->getInt("n", n) == kResultOk) {
            note("hello " + std::to_string(n));
        }
        return kResultOk;
    }

    /// What the view does: as a person moving the gain would.
    void edit(Vst::ParamValue value)
    {
        m_gain = value;
        if (m_handler != nullptr) {
            (void)m_handler->performEdit(kGain, value);
        }
    }
    void hold(bool begin)
    {
        if (m_handler != nullptr) {
            (void)(begin ? m_handler->beginEdit(kGain) : m_handler->endEdit(kGain));
        }
    }
    void zoom(std::int32_t zoom)
    {
        m_zoom = zoom;
    }

private:
    void tell(const char *id)
    {
        void *made = nullptr;
        TUID messageId;
        std::memcpy(messageId, Vst::IMessage_iid, sizeof(TUID));
        if (m_peer == nullptr || m_host == nullptr || m_host->createInstance(messageId, messageId, &made) != kResultOk
            || made == nullptr) {
            return;
        }
        auto *message = static_cast<Vst::IMessage *>(made);
        message->setMessageID(id);
        (void)m_peer->notify(message);
        message->release();
    }

    Vst::IHostApplication *m_host = nullptr;
    Vst::IConnectionPoint *m_peer = nullptr;
    Vst::IComponentHandler *m_handler = nullptr;
    double m_gain = 0.25;
    std::int32_t m_colour = 0;
    std::int32_t m_zoom = 1;
    bool m_heardLevel = false;
};

View::View(Controller &controller)
    : m_controller(controller)
{
    m_controller.addRef();
}

View::~View()
{
    if (m_fd >= 0) {
        ::close(m_fd);
    }
    // A view never attached is let go of without being removed.
    if (m_holdsController) {
        m_controller.release();
    }
}

tresult PLUGIN_API View::removed()
{
    if (m_loop != nullptr) {
        (void)m_loop->unregisterTimer(this);
        (void)m_loop->unregisterEventHandler(this);
        m_loop->release();
        m_loop = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    if (m_holdsController) {
        m_holdsController = false;
        m_controller.release();
    }
    note("removed");
    return kResultOk;
}

void PLUGIN_API View::onTimer()
{
    ++m_ticks;
    if (m_ticks == 1) {
        // A larger window, as a view that grows to fit would ask.
        ViewRect larger(0, 0, 320, 200);
        if (m_frame != nullptr) {
            (void)m_frame->resizeView(this, &larger);
        }
        m_controller.zoom(2);
        m_controller.hold(true);
        m_controller.edit(0.0625); // 0.25
    } else if (m_ticks == 1 + kHeldFor) {
        m_controller.edit(0.075); // 0.3
        m_controller.hold(false);
    }
}

// --- the factory ---

class Factory final : public IPluginFactory2
{
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **object) override
    {
        if (same(iid, FUnknown_iid) || same(iid, IPluginFactory_iid) || same(iid, IPluginFactory2_iid)) {
            *object = static_cast<IPluginFactory2 *>(this);
            addRef();
            return kResultOk;
        }
        *object = nullptr;
        return kNoInterface;
    }
    // One for the library, made once.
    uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    uint32 PLUGIN_API release() override
    {
        return 1;
    }

    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *info) override
    {
        *info = PFactoryInfo("Reverblabr", "", "", PFactoryInfo::kUnicode);
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override
    {
        return 3;
    }
    tresult PLUGIN_API getClassInfo(int32 index, PClassInfo *info) override
    {
        PClassInfo2 full{};
        if (getClassInfo2(index, &full) != kResultOk) {
            return kInvalidArgument;
        }
        *info = PClassInfo(full.cid, full.cardinality, full.category, full.name);
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 index, PClassInfo2 *info) override
    {
        switch (index) {
        case 0:
            *info = PClassInfo2(kStereoId,
                                PClassInfo::kManyInstances,
                                kVstAudioEffectClass,
                                "Reverblabr test gain",
                                0,
                                "Fx|Tools",
                                "Reverblabr",
                                "1",
                                kVstVersionString);
            return kResultOk;
        case 1:
            *info = PClassInfo2(kMonoId,
                                PClassInfo::kManyInstances,
                                kVstAudioEffectClass,
                                "Reverblabr test gain, mono",
                                0,
                                "Fx|Tools|Mono",
                                "Reverblabr",
                                "1",
                                kVstVersionString);
            return kResultOk;
        case 2:
            *info = PClassInfo2(kControllerId,
                                PClassInfo::kManyInstances,
                                kVstComponentControllerClass,
                                "Reverblabr test gain controller",
                                0,
                                "",
                                "Reverblabr",
                                "1",
                                kVstVersionString);
            return kResultOk;
        default:
            return kInvalidArgument;
        }
    }
    tresult PLUGIN_API createInstance(FIDString classId, FIDString iid, void **object) override
    {
        FUnknown *made = nullptr;
        if (same(classId, kStereoId) || same(classId, kMonoId)) {
            made = static_cast<Vst::IComponent *>(new Component(same(classId, kMonoId)));
        } else if (same(classId, kControllerId)) {
            made = static_cast<Vst::IEditController *>(new Controller());
        }
        if (made == nullptr) {
            *object = nullptr;
            return kNoInterface;
        }
        const tresult found = made->queryInterface(iid, object);
        made->release();
        return found;
    }
};

} // namespace

#pragma GCC diagnostic pop

extern "C" {

SMTG_EXPORT_SYMBOL bool ModuleEntry(void *)
{
    return true;
}

SMTG_EXPORT_SYMBOL bool ModuleExit()
{
    return true;
}

SMTG_EXPORT_SYMBOL IPluginFactory *GetPluginFactory()
{
    static Factory factory;
    return &factory;
}

} // extern "C"

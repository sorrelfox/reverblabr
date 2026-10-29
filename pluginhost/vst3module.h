// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "plugin/formats.h"
#include "rt/realtime.h"

#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/base/ibstream.h>
#include <pluginterfaces/base/ipluginbase.h>
#include <pluginterfaces/vst/ivstattributes.h>
#include <pluginterfaces/vst/ivstcomponent.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/ivstmessage.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

// VST3 hosting (ADR-0028) through the SDK's interface headers alone: what
// a host has to be to a plugin is written here, in the host's own words,
// and nothing of the SDK's helper sources is compiled in. **Every interface
// is asked for by its id**, the TUID each header declares beside it; the
// SDK's FUID objects would need its sources.

// **The format's interfaces have no virtual destructors**: an object is
// let go through release(), never deleted through one of them, so the
// warning about it is not for these classes.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor"

namespace rb::pluginhost
{

/// **A reference to a VST3 object, released when it goes**: what the
/// plugin hands out, counted as it counts them.
template <typename T>
class Vst3Ref
{
public:
    Vst3Ref() = default;
    /// Takes over \a object, which has been counted for this reference.
    explicit Vst3Ref(T *object) noexcept
        : m_object(object)
    {
    }
    ~Vst3Ref()
    {
        reset();
    }
    Vst3Ref(const Vst3Ref &other) noexcept
        : m_object(other.m_object)
    {
        if (m_object != nullptr) {
            m_object->addRef();
        }
    }
    Vst3Ref &operator=(const Vst3Ref &other) noexcept
    {
        if (this != &other) {
            Vst3Ref copy(other);
            std::swap(m_object, copy.m_object);
        }
        return *this;
    }
    Vst3Ref(Vst3Ref &&other) noexcept
        : m_object(std::exchange(other.m_object, nullptr))
    {
    }
    Vst3Ref &operator=(Vst3Ref &&other) noexcept
    {
        if (this != &other) {
            reset();
            m_object = std::exchange(other.m_object, nullptr);
        }
        return *this;
    }

    void reset() noexcept
    {
        if (T *object = std::exchange(m_object, nullptr); object != nullptr) {
            object->release();
        }
    }
    [[nodiscard]] T *get() const noexcept
    {
        return m_object;
    }
    T *operator->() const noexcept
    {
        return m_object;
    }
    explicit operator bool() const noexcept
    {
        return m_object != nullptr;
    }

private:
    T *m_object = nullptr;
};

/// \a from's interface \a iid, as a T, or nothing.
template <typename T>
[[nodiscard]] Vst3Ref<T> vst3Query(Steinberg::FUnknown *from, const Steinberg::TUID iid)
{
    void *found = nullptr;
    if (from == nullptr || from->queryInterface(iid, &found) != Steinberg::kResultOk || found == nullptr) {
        return {};
    }
    return Vst3Ref<T>(static_cast<T *>(found));
}

/// A class id as the format writes it: 32 hexadecimal digits, upper case.
[[nodiscard]] std::string vst3ClassId(const Steinberg::TUID id);
/// And back, into \a id, upper or lower case; false when \a text is not
/// one.
[[nodiscard]] bool vst3ClassIdFrom(std::string_view text, Steinberg::TUID id);

/// UTF-16 text from a plugin, as UTF-8; and UTF-8 into \a size characters
/// of UTF-16, cut short where it must be.
[[nodiscard]] std::string vst3Text(const Steinberg::char16 *text, std::size_t size = 0);
void vst3SetText(Steinberg::char16 *into, std::size_t size, std::string_view text);

/// **What a host is to every VST3 plugin**: its name, and the messages and
/// attribute lists plugins make through it to talk between their two
/// halves. One for the process; it is never let go.
[[nodiscard]] Steinberg::FUnknown *vst3HostContext();

/// **A stream over bytes in memory**, which is what a plugin writes its
/// state into and reads it back from.
class Vst3Stream final : public Steinberg::IBStream, public Steinberg::ISizeableStream
{
public:
    Vst3Stream() = default;
    explicit Vst3Stream(std::string bytes)
        : m_bytes(std::move(bytes))
    {
    }
    Vst3Stream(const Vst3Stream &) = delete;
    Vst3Stream &operator=(const Vst3Stream &) = delete;

    [[nodiscard]] const std::string &bytes() const noexcept
    {
        return m_bytes;
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::tresult PLUGIN_API read(void *buffer, Steinberg::int32 count, Steinberg::int32 *done) override;
    Steinberg::tresult PLUGIN_API write(void *buffer, Steinberg::int32 count, Steinberg::int32 *done) override;
    Steinberg::tresult PLUGIN_API seek(Steinberg::int64 position,
                                       Steinberg::int32 mode,
                                       Steinberg::int64 *result) override;
    Steinberg::tresult PLUGIN_API tell(Steinberg::int64 *position) override;
    Steinberg::tresult PLUGIN_API getStreamSize(Steinberg::int64 &size) override;
    Steinberg::tresult PLUGIN_API setStreamSize(Steinberg::int64 size) override;

private:
    std::string m_bytes;
    std::size_t m_at = 0;
};

/// **One `.vst3` bundle, loaded**: its library, entered for as long as the
/// host runs, and the factory its plugins are made by.
class Vst3Module
{
public:
    /// The bundle at \a path (or a lone library), or nothing when it is not
    /// one this host can load.
    [[nodiscard]] static std::shared_ptr<Vst3Module> open(const std::string &path);

    ~Vst3Module();
    Vst3Module(const Vst3Module &) = delete;
    Vst3Module &operator=(const Vst3Module &) = delete;

    /// One class the factory makes.
    struct Class {
        std::string id;
        std::string name;
        std::string vendor;
        /// "Audio Module Class" for a plugin's sound.
        std::string category;
        /// What it is, as "Fx|Dynamics".
        std::string subCategories;
    };
    [[nodiscard]] std::vector<Class> classes() const;
    /// The vendor the factory gives, for a class that gives none.
    [[nodiscard]] std::string vendor() const;
    /// A new object of class \a id, asked for as \a iid, or null.
    [[nodiscard]] void *create(const Steinberg::TUID id, const Steinberg::TUID iid) const;

    [[nodiscard]] const std::string &path() const noexcept
    {
        return m_path;
    }

private:
    Vst3Module() = default;

    std::string m_path;
    void *m_handle = nullptr;
    Steinberg::IPluginFactory *m_factory = nullptr;
    bool (*m_exit)() = nullptr;
};

/// **What a bundle's `moduleinfo.json` says of its classes**, read without
/// loading its code; nothing when it has none, or it cannot be read.
[[nodiscard]] std::optional<std::vector<Vst3Module::Class>> vst3ModuleInfo(const std::string &bundle);

/// **One side's end of the line between a plugin's two halves**: what one
/// half sends through it reaches the halves on the other side, but only
/// from the host's main thread. A message sent from anywhere else (the
/// audio thread, from inside a process call) is refused and dropped, so
/// that the other half never runs there, and nothing it does on receiving
/// it happens on the audio thread. The format allows a host this, as the
/// SDK's own hosts do it.
class Vst3Relay final : public Steinberg::Vst::IConnectionPoint
{
public:
    /// Messages are passed on when they are sent from the thread that
    /// makes the relay.
    Vst3Relay();
    Vst3Relay(const Vst3Relay &) = delete;
    Vst3Relay &operator=(const Vst3Relay &) = delete;

    /// Where messages go from now on: every point added, in turn.
    void add(Steinberg::Vst::IConnectionPoint *to);
    void remove(Steinberg::Vst::IConnectionPoint *to);
    /// How many messages were dropped for being sent from another thread.
    [[nodiscard]] std::size_t dropped() const noexcept
    {
        return m_dropped.load(std::memory_order_relaxed);
    }

    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **object) override;
    Steinberg::uint32 PLUGIN_API addRef() override;
    Steinberg::uint32 PLUGIN_API release() override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint *other) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint *other) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage *message) override;

private:
    std::thread::id m_main;
    std::vector<Steinberg::Vst::IConnectionPoint *> m_to;
    std::atomic<std::size_t> m_dropped{0};
};

/// **A plugin's two halves, made and joined**: its component, which makes
/// the sound, and its edit controller, which holds what a person sees,
/// with the state of the one given to the other. Either may be the same
/// object, as a plugin of one piece has it.
struct Vst3Halves {
    Vst3Ref<Steinberg::Vst::IComponent> component;
    Vst3Ref<Steinberg::Vst::IEditController> controller;
    /// The two connection points, when the plugin has them, each joined
    /// to a relay of the host's that passes messages on to the other:
    /// toController from the component, toComponents from the controller
    /// to every component (a mono plugin's second instance too).
    Vst3Ref<Steinberg::Vst::IConnectionPoint> componentPoint;
    Vst3Ref<Steinberg::Vst::IConnectionPoint> controllerPoint;
    std::unique_ptr<Vst3Relay> toController;
    std::unique_ptr<Vst3Relay> toComponents;
    bool oneObject = false;

    Vst3Halves() = default;
    ~Vst3Halves();
    Vst3Halves(const Vst3Halves &) = delete;
    Vst3Halves &operator=(const Vst3Halves &) = delete;
    Vst3Halves(Vst3Halves &&) = default;
    Vst3Halves &operator=(Vst3Halves &&) = delete;
};

/// Class \a id of \a module, its two halves made and initialised, or
/// nothing. \a handler is told what the controller does, when given.
[[nodiscard]] std::optional<Vst3Halves>
makeVst3Halves(const Vst3Module &module, const std::string &id, Steinberg::Vst::IComponentHandler *handler);
/// Only the component, for a second instance that makes the other side's
/// sound.
[[nodiscard]] Vst3Ref<Steinberg::Vst::IComponent> makeVst3Component(const Vst3Module &module, const std::string &id);

/// The channels of a component's first audio bus each way, the one the
/// block's ports are; 0 when it has none.
[[nodiscard]] std::uint32_t vst3MainChannels(Steinberg::Vst::IComponent *component,
                                             Steinberg::Vst::BusDirection direction);

/// **How one parameter's values map to the 0 to 1 the plugin is told**:
/// the plain value at evenly spaced points of that range. A stepped
/// parameter has one point a step, which is exact; a continuous one has
/// enough that a value between two points is near enough what the plugin
/// would make of it, and between them it is found on a straight line, or
/// on a curve of equal ratios when the parameter is logarithmic.
struct Vst3Curve {
    std::vector<double> plain;
    bool stepped = false;
    bool logarithmic = false;

    /// The 0 to 1 that \a value is. On the audio thread.
    [[nodiscard]] double normalizedOf(double value) const noexcept RB_NONBLOCKING;
    /// The plain value at \a normalized.
    [[nodiscard]] double plainOf(double normalized) const noexcept RB_NONBLOCKING;
};

/// **A VST3 plugin's settings, as the engine numbers them**: every
/// parameter that is neither hidden nor read-only nor a program change,
/// in the controller's order, with \a ids and \a curves (when given)
/// filled in the same order. A value is the plain value, the one the
/// plugin shows. A parameter of steps is a choice when its steps have
/// words, a switch when they are off and on, a whole number when they are
/// numbers; one that is not stepped is a number between its two ends.
[[nodiscard]] std::vector<plugin::PluginParam> vst3Params(Steinberg::Vst::IEditController *controller,
                                                          std::vector<Steinberg::Vst::ParamID> *ids = nullptr,
                                                          std::vector<Vst3Curve> *curves = nullptr);

/// **Every plugin in the bundle at \a path**, described: each is made and
/// asked, then let go. The bundle's `moduleinfo.json`, where it has one,
/// says which classes are plugins without loading anything, and a bundle
/// that says it has none is not loaded at all. \a found is told of each as
/// soon as it is described. False when the bundle cannot be loaded.
[[nodiscard]] bool describeVst3(const std::string &path,
                                const std::function<void(const plugin::PluginDescription &)> &found);

/// **A `.vstpreset` file's contents**: whose it is, and the two halves'
/// states. It is also what a VST3 block keeps as its state, as base64.
struct Vst3Preset {
    std::string classId;
    std::string component;
    std::optional<std::string> controller;
};
[[nodiscard]] std::string writeVst3Preset(const Vst3Preset &preset);
[[nodiscard]] std::optional<Vst3Preset> readVst3Preset(const std::string &bytes);

/// **Where VST3 presets are**, as other hosts keep them: under
/// `~/.vst3/presets` (the user's, where one is saved) and the system's
/// `/usr/share/vst3/presets` and `/usr/local/share/vst3/presets`, each in
/// a folder of the plugin's vendor and in one of its name. The user's first.
[[nodiscard]] std::vector<std::string> vst3PresetFolders(const std::string &vendor, const std::string &name);

/// **A class id as this host writes it**, whatever its case; nothing when
/// \a text is not one.
[[nodiscard]] std::optional<std::string> vst3CanonicalId(std::string_view text);
/// Whose preset the file at \a path is, from its header alone; nothing
/// when it is not a `.vstpreset`.
[[nodiscard]] std::optional<std::string> vst3PresetClassOf(const std::string &path);
/// **A name as a folder or a file can be called**: one name, no folders in
/// it, no control characters, never "." or ".." or nothing.
[[nodiscard]] std::string vst3FileName(std::string_view name);

} // namespace rb::pluginhost

#pragma GCC diagnostic pop

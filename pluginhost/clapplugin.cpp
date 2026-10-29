// SPDX-FileCopyrightText: 2026 Reverblabr contributors
// SPDX-License-Identifier: MPL-2.0
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "clapplugin.h"

#include "base64.h"
#include "x11window.h"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string_view>

namespace rb::pluginhost
{

namespace
{

/// A stepped parameter with more whole numbers than this is a number,
/// whatever its words.
constexpr double kMostSteps = 128.0;
/// A stepped parameter whose words change more often than this across its
/// range is a number stepped finely, not a choice.
constexpr std::size_t kMostChoices = 64;
/// How finely the place where a stepped parameter's words change is found:
/// this part of its range.
constexpr double kFinest = 1.0 / 1024.0;
/// Room for what the plugin says a value is.
constexpr std::size_t kWordsLength = 256;

/// Whether this thread is acting for the audio thread (ClapHost::ActingAsAudio).
thread_local bool t_actingAsAudio = false;

/// **The plugin's own process call**, which CLAP puts on the audio thread:
/// made for it, which the compiler cannot know of a function pointer. The
/// claim is made here, once, as the LV2 host makes it for run().
clap_process_status processOf(const clap_plugin *plugin, const clap_process *process) noexcept RB_NONBLOCKING
{
    using Process = clap_process_status (*)(const clap_plugin *, const clap_process *) RB_NONBLOCKING;
    const auto call = reinterpret_cast<Process>(plugin->process);
    return call(plugin, process);
}

bool startProcessingOf(const clap_plugin *plugin) noexcept RB_NONBLOCKING
{
    using Start = bool (*)(const clap_plugin *) RB_NONBLOCKING;
    const auto call = reinterpret_cast<Start>(plugin->start_processing);
    return call(plugin);
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

/// The channels of each of \a plugin's audio ports, inputs or outputs.
[[nodiscard]] std::vector<std::uint32_t> portsOf(const clap_plugin *plugin, bool input)
{
    std::vector<std::uint32_t> channels;
    const auto *ports =
        static_cast<const clap_plugin_audio_ports *>(plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));
    if (ports == nullptr) {
        return channels;
    }
    const std::uint32_t count = ports->count(plugin, input);
    for (std::uint32_t index = 0; index < count; ++index) {
        clap_audio_port_info info{};
        if (ports->get(plugin, index, input, &info)) {
            channels.push_back(info.channel_count);
        }
    }
    return channels;
}

[[nodiscard]] std::uint32_t sum(const std::vector<std::uint32_t> &channels)
{
    std::uint32_t total = 0;
    for (const std::uint32_t count : channels) {
        total += count;
    }
    return total;
}

[[nodiscard]] std::string lowered(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

/// Whether \a words are only a number: what a stepped parameter that is a
/// number says of its values.
[[nodiscard]] bool isNumber(const std::string &words)
{
    if (words.empty()) {
        return false;
    }
    char *end = nullptr;
    (void)std::strtod(words.c_str(), &end);
    while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    return end != nullptr && end != words.c_str() && *end == '\0';
}

/// **What a stepped parameter's steps are**, from what the plugin calls its
/// values (see clapParams()).
void describeSteps(const clap_plugin *plugin,
                   const clap_plugin_params *params,
                   const clap_param_info &info,
                   plugin::PluginParam &param)
{
    const auto words = [&](double value) {
        std::array<char, kWordsLength> text{};
        if (!params->value_to_text(plugin, info.id, value, text.data(), static_cast<std::uint32_t>(text.size()))) {
            return std::string{};
        }
        return std::string{text.data()};
    };
    const double low = std::min(info.min_value, info.max_value);
    const double high = std::max(info.min_value, info.max_value);
    const double span = high - low;
    const bool whole = low == std::floor(low) && high == std::floor(high);

    if (whole && span > kMostSteps) {
        param.kind = plugin::PluginParam::Kind::Int;
        return;
    }
    if (whole && span >= 2.0) {
        bool numbers = true;
        std::vector<std::pair<float, std::string>> choices;
        for (double value = low; value <= high; value += 1.0) {
            std::string said = words(value);
            numbers = numbers && isNumber(said);
            choices.emplace_back(static_cast<float>(value), std::move(said));
        }
        if (numbers) {
            param.kind = plugin::PluginParam::Kind::Int;
        } else {
            param.kind = plugin::PluginParam::Kind::Choice;
            param.choices = std::move(choices);
        }
        return;
    }
    if (whole && span == 1.0) {
        // Two whole numbers: two values, unless the words change between
        // them too, which is a plugin stepping through fractions.
        const std::string first = words(low);
        const std::string last = words(high);
        const std::string middle = words(low + span / 2.0);
        if (middle == first || middle == last) {
            const bool onOff = lowered(first) == "off" && lowered(last) == "on";
            param.kind = onOff ? plugin::PluginParam::Kind::Toggle : plugin::PluginParam::Kind::Choice;
            if (!onOff) {
                param.choices = {{static_cast<float>(low), first}, {static_cast<float>(high), last}};
            }
            return;
        }
    }
    if (span <= 0.0) {
        param.kind = plugin::PluginParam::Kind::Float;
        return;
    }
    // **Where the words change**, found by halving: a stretch whose two ends
    // say the same is taken to say it throughout, and one whose ends differ
    // is halved until the change is pinned down. Each stretch of the same
    // words is one choice, at a whole number inside it if there is one,
    // otherwise at its middle, which is inside whatever the plugin rounds
    // to.
    const double finest = span * kFinest;
    std::vector<std::pair<double, std::string>> starts{{low, words(low)}};
    bool tooMany = false;
    const auto split = [&](auto &self, double from, const std::string &left, double to, const std::string &right) {
        if (left == right || tooMany) {
            return;
        }
        if (to - from <= finest) {
            starts.emplace_back(to, right);
            tooMany = starts.size() > kMostChoices;
            return;
        }
        const double middle = (from + to) / 2.0;
        const std::string said = words(middle);
        self(self, from, left, middle, said);
        self(self, middle, said, to, right);
    };
    split(split, low, starts.front().second, high, words(high));
    if (tooMany) {
        // Words for every step: a number, stepped finer than a choice.
        param.kind = plugin::PluginParam::Kind::Float;
        return;
    }
    std::vector<std::pair<float, std::string>> choices;
    for (std::size_t index = 0; index < starts.size(); ++index) {
        const double from = starts[index].first;
        const double to = index + 1 < starts.size() ? starts[index + 1].first - finest : high;
        double value = (from + to) / 2.0;
        if (const double inside = std::ceil(from); inside <= to) {
            value = inside;
        }
        choices.emplace_back(static_cast<float>(value), starts[index].second);
    }
    param.kind = plugin::PluginParam::Kind::Choice;
    param.choices = std::move(choices);
}

/// **The plugin's own word for what it is**, from its features: the first
/// that says what it does, rather than that it is an effect or how many
/// channels it has. "compressor" becomes "Compressor".
[[nodiscard]] std::string categoryOf(const clap_plugin_descriptor *descriptor)
{
    static constexpr std::array<std::string_view, 9> kKinds = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                                               CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
                                                               CLAP_PLUGIN_FEATURE_NOTE_EFFECT,
                                                               CLAP_PLUGIN_FEATURE_NOTE_DETECTOR,
                                                               CLAP_PLUGIN_FEATURE_ANALYZER,
                                                               CLAP_PLUGIN_FEATURE_MONO,
                                                               CLAP_PLUGIN_FEATURE_STEREO,
                                                               CLAP_PLUGIN_FEATURE_SURROUND,
                                                               CLAP_PLUGIN_FEATURE_AMBISONIC};
    std::string found;
    for (const char *const *feature = descriptor->features; feature != nullptr && *feature != nullptr; ++feature) {
        if (std::ranges::find(kKinds, std::string_view{*feature}) == kKinds.end()) {
            found = *feature;
            break;
        }
        if (found.empty() && std::string_view{*feature} == CLAP_PLUGIN_FEATURE_ANALYZER) {
            found = *feature;
        }
    }
    std::ranges::replace(found, '-', ' ');
    if (!found.empty()) {
        found[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(found[0])));
    }
    return found;
}

/// **The changes of one cycle, as CLAP's input events.**
struct InputEvents {
    clap_input_events events{};
    const std::vector<clap_event_param_value> *items = nullptr;
    std::uint32_t count = 0;
};

std::uint32_t sizeOf(const clap_input_events *list) noexcept
{
    return static_cast<const InputEvents *>(list->ctx)->count;
}

const clap_event_header *eventAt(const clap_input_events *list, std::uint32_t index) noexcept
{
    const auto *events = static_cast<const InputEvents *>(list->ctx);
    return index < events->count ? &(*events->items)[index].header : nullptr;
}

/// **What the plugin says back**: a value it changed itself is taken as
/// what the setting now is. While its window listens, the first instance's
/// values and the gestures round them go on to the main thread too, and a
/// mono plugin run twice has the other instance hear them next cycle.
struct OutputEvents {
    clap_output_events events{};
    const std::vector<std::pair<clap_id, std::uint32_t>> *byId = nullptr;
    std::vector<std::atomic<float>> *values = nullptr;
    /// Where what a person did in the window goes, or null.
    rt::SpscQueue<ClapSaid> *said = nullptr;
    /// The parameters changed this cycle, for the other instance; or null.
    std::vector<std::uint8_t> *changed = nullptr;
    std::vector<std::uint32_t> *changedList = nullptr;
    std::uint32_t *changedCount = nullptr;
};

/// The index of the parameter the plugin calls \a id, or nothing.
[[nodiscard]] std::optional<std::uint32_t> indexOf(const OutputEvents &out, clap_id id) noexcept RB_NONBLOCKING
{
    const auto found = std::ranges::lower_bound(
        *out.byId, id, {}, [](const std::pair<clap_id, std::uint32_t> &entry) noexcept { return entry.first; });
    if (found != out.byId->end() && found->first == id) {
        return found->second;
    }
    return std::nullopt;
}

bool pushed(const clap_output_events *list, const clap_event_header *event) noexcept RB_NONBLOCKING
{
    const auto *out = static_cast<const OutputEvents *>(list->ctx);
    if (event->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        return true;
    }
    if (event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || event->type == CLAP_EVENT_PARAM_GESTURE_END) {
        const auto *gesture = reinterpret_cast<const clap_event_param_gesture *>(event);
        if (const std::optional<std::uint32_t> index = indexOf(*out, gesture->param_id);
            index && out->said != nullptr) {
            (void)out->said->tryPush(ClapSaid{
                event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? ClapSaid::Begin : ClapSaid::End, *index, 0.0f});
        }
        return true;
    }
    if (event->type != CLAP_EVENT_PARAM_VALUE) {
        return true;
    }
    const auto *change = reinterpret_cast<const clap_event_param_value *>(event);
    const std::optional<std::uint32_t> index = indexOf(*out, change->param_id);
    if (!index) {
        return true;
    }
    const auto value = static_cast<float>(change->value);
    // **Only a value that moved**: one the host sent and the plugin says
    // back is no news, and would come back to the engine as a person's.
    const float before = (*out->values)[*index].exchange(value, std::memory_order_relaxed);
    if (before == value) {
        return true;
    }
    if (out->said != nullptr) {
        (void)out->said->tryPush(ClapSaid{ClapSaid::Value, *index, value});
    }
    if (out->changed != nullptr && (*out->changed)[*index] == 0) {
        (*out->changed)[*index] = 1;
        (*out->changedList)[(*out->changedCount)++] = *index;
    }
    return true;
}

} // namespace

// --- ClapLibrary ---

std::shared_ptr<ClapLibrary> ClapLibrary::open(const std::string &path)
{
    void *handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        std::fprintf(stderr, "not a loadable CLAP file: %s\n", ::dlerror());
        return nullptr;
    }
    const auto *entry = static_cast<const clap_plugin_entry *>(::dlsym(handle, "clap_entry"));
    if (entry == nullptr || !clap_version_is_compatible(entry->clap_version) || !entry->init(path.c_str())) {
        std::fprintf(stderr, "not a CLAP file this host can use: %s\n", path.c_str());
        ::dlclose(handle);
        return nullptr;
    }
    const auto *factory = static_cast<const clap_plugin_factory *>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr) {
        entry->deinit();
        ::dlclose(handle);
        return nullptr;
    }
    std::shared_ptr<ClapLibrary> library(new ClapLibrary());
    library->m_handle = handle;
    library->m_entry = entry;
    library->m_factory = factory;
    return library;
}

ClapLibrary::~ClapLibrary()
{
    // **Not unloaded**: what a plugin leaves running in the file (a thread,
    // a destructor at exit) must still have its code there. The host ends
    // soon after anyway.
    m_entry->deinit();
}

std::uint32_t ClapLibrary::count() const
{
    return m_factory->get_plugin_count(m_factory);
}

const clap_plugin_descriptor *ClapLibrary::descriptorAt(std::uint32_t index) const
{
    return m_factory->get_plugin_descriptor(m_factory, index);
}

const clap_preset_discovery_factory *ClapLibrary::presetFactory() const
{
    const void *factory = m_entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID);
    if (factory == nullptr) {
        factory = m_entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID_COMPAT);
    }
    return static_cast<const clap_preset_discovery_factory *>(factory);
}

const clap_plugin *ClapLibrary::create(const clap_host *host, std::string_view id) const
{
    const std::string wanted{id};
    const clap_plugin *plugin = m_factory->create_plugin(m_factory, host, wanted.c_str());
    if (plugin == nullptr) {
        return nullptr;
    }
    if (!plugin->init(plugin)) {
        plugin->destroy(plugin);
        return nullptr;
    }
    return plugin;
}

// --- ClapHost ---

ClapHost::ClapHost()
    : m_main(std::this_thread::get_id())
{
    m_host.clap_version = CLAP_VERSION;
    m_host.host_data = this;
    m_host.name = "Reverblabr";
    m_host.vendor = "Reverblabr";
    m_host.url = "";
    m_host.version = "1";
    m_host.get_extension = &ClapHost::extension;
    m_host.request_restart = [](const clap_host *host) { self(host)->m_restart.store(true); };
    m_host.request_process = [](const clap_host *) {
        // The host's node always runs: there is nothing to wake.
    };
    m_host.request_callback = [](const clap_host *host) { self(host)->m_callback.store(true); };
}

ClapHost::~ClapHost()
{
    setLoop(nullptr);
}

ClapHost *ClapHost::self(const clap_host *host)
{
    return static_cast<ClapHost *>(host->host_data);
}

void ClapHost::setLoop(MainLoop *loop)
{
    if (m_loop != nullptr) {
        for (auto &[id, timer] : m_timers) {
            if (timer.watch) {
                m_loop->remove(*timer.watch);
            }
            timer.watch.reset();
        }
        for (auto &[fd, watched] : m_fds) {
            if (watched.watch) {
                m_loop->remove(*watched.watch);
            }
            watched.watch.reset();
        }
    }
    m_loop = loop;
    if (m_loop != nullptr) {
        for (auto &[id, timer] : m_timers) {
            watchTimer(id, timer);
        }
        for (auto &[fd, watched] : m_fds) {
            watchFd(fd, watched);
        }
    }
}

void ClapHost::watchTimer(clap_id id, Timer &timer)
{
    if (m_loop == nullptr || timer.watch) {
        return;
    }
    timer.watch = m_loop->addTimer(std::chrono::milliseconds{std::max<std::uint32_t>(timer.period, 1)}, [this, id] {
        const auto *support = m_plugin != nullptr
            ? static_cast<const clap_plugin_timer_support *>(m_plugin->get_extension(m_plugin, CLAP_EXT_TIMER_SUPPORT))
            : nullptr;
        if (support != nullptr && support->on_timer != nullptr) {
            support->on_timer(m_plugin, id);
        }
    });
}

void ClapHost::watchFd(int fd, Fd &watched)
{
    if (m_loop == nullptr || watched.watch) {
        return;
    }
    std::uint32_t events = 0;
    events |= (watched.flags & CLAP_POSIX_FD_READ) != 0 ? std::uint32_t{MainLoop::Readable} : 0U;
    events |= (watched.flags & CLAP_POSIX_FD_WRITE) != 0 ? std::uint32_t{MainLoop::Writable} : 0U;
    watched.watch = m_loop->addFd(fd, events, [this, fd](std::uint32_t found) {
        const auto *support = m_plugin != nullptr ? static_cast<const clap_plugin_posix_fd_support *>(
                                                        m_plugin->get_extension(m_plugin, CLAP_EXT_POSIX_FD_SUPPORT))
                                                  : nullptr;
        if (support == nullptr || support->on_fd == nullptr) {
            return;
        }
        clap_posix_fd_flags_t flags = 0;
        flags |= (found & MainLoop::Readable) != 0 ? clap_posix_fd_flags_t{CLAP_POSIX_FD_READ} : 0U;
        flags |= (found & MainLoop::Writable) != 0 ? clap_posix_fd_flags_t{CLAP_POSIX_FD_WRITE} : 0U;
        flags |= (found & MainLoop::Failed) != 0 ? clap_posix_fd_flags_t{CLAP_POSIX_FD_ERROR} : 0U;
        support->on_fd(m_plugin, fd, flags);
    });
}

std::optional<std::string> ClapHost::takeLoaded()
{
    const std::lock_guard lock(m_loadedLock);
    return std::exchange(m_loaded, std::nullopt);
}

ClapHost::ActingAsAudio::ActingAsAudio()
{
    t_actingAsAudio = true;
}

ClapHost::ActingAsAudio::~ActingAsAudio()
{
    t_actingAsAudio = false;
}

const void *ClapHost::extension(const clap_host *, const char *id)
{
    static const clap_host_log log{[](const clap_host *, clap_log_severity severity, const char *message) {
        if (severity >= CLAP_LOG_WARNING) {
            std::fprintf(stderr, "plugin: %s\n", message);
        }
    }};
    static const clap_host_thread_check threads{
        [](const clap_host *host) { return !t_actingAsAudio && std::this_thread::get_id() == self(host)->m_main; },
        [](const clap_host *host) { return t_actingAsAudio || std::this_thread::get_id() != self(host)->m_main; }};
    static const clap_host_params params{[](const clap_host *host, clap_param_rescan_flags flags) {
                                             // **Its values moved by themselves**, a preset in its window:
                                             // read again on the main thread's next turn. A plugin whose
                                             // parameters themselves changed would need to be described
                                             // again, which a restart of its host does.
                                             if ((flags & (CLAP_PARAM_RESCAN_VALUES | CLAP_PARAM_RESCAN_ALL)) != 0) {
                                                 self(host)->m_rescan.store(true);
                                             }
                                         },
                                         [](const clap_host *, clap_id, clap_param_clear_flags) { },
                                         [](const clap_host *) {
                                             // The host's node always runs, so a change is flushed with the
                                             // next cycle anyway.
                                         }};
    static const clap_host_latency latency{[](const clap_host *) {
        // Asked for again by the host's timer, which is where a latency is
        // told to the engine.
    }};
    static const clap_host_state state{[](const clap_host *) { }};
    static const clap_host_audio_ports ports{[](const clap_host *, std::uint32_t) { return false; },
                                             [](const clap_host *, std::uint32_t) { }};
    // **The window's**, asked on the main thread.
    static const clap_host_gui gui{
        [](const clap_host *host) {
            if (self(host)->resizeHintsChanged) {
                self(host)->resizeHintsChanged();
            }
        },
        [](const clap_host *host, std::uint32_t width, std::uint32_t height) {
            return self(host)->requestResize ? self(host)->requestResize(width, height) : false;
        },
        [](const clap_host *host) { return self(host)->requestShow ? self(host)->requestShow(true) : false; },
        [](const clap_host *host) { return self(host)->requestShow ? self(host)->requestShow(false) : false; },
        [](const clap_host *host, bool) {
            // A person closed its floating window: taken down on the main
            // thread's next turn, not from inside the plugin's own call.
            self(host)->m_closed.store(true);
        }};
    static const clap_host_timer_support timers{[](const clap_host *host, std::uint32_t period, clap_id *timerId) {
                                                    ClapHost *me = self(host);
                                                    const clap_id made = ++me->m_nextTimer;
                                                    Timer &timer = me->m_timers[made];
                                                    timer.period = period;
                                                    me->watchTimer(made, timer);
                                                    *timerId = made;
                                                    return true;
                                                },
                                                [](const clap_host *host, clap_id timerId) {
                                                    ClapHost *me = self(host);
                                                    const auto found = me->m_timers.find(timerId);
                                                    if (found == me->m_timers.end()) {
                                                        return false;
                                                    }
                                                    if (found->second.watch && me->m_loop != nullptr) {
                                                        me->m_loop->remove(*found->second.watch);
                                                    }
                                                    me->m_timers.erase(found);
                                                    return true;
                                                }};
    static const clap_host_posix_fd_support fds{
        [](const clap_host *host, int fd, clap_posix_fd_flags_t flags) {
            ClapHost *me = self(host);
            if (fd < 0 || me->m_fds.contains(fd)) {
                return false;
            }
            Fd &watched = me->m_fds[fd];
            watched.flags = flags;
            me->watchFd(fd, watched);
            return true;
        },
        [](const clap_host *host, int fd, clap_posix_fd_flags_t flags) {
            ClapHost *me = self(host);
            const auto found = me->m_fds.find(fd);
            if (found == me->m_fds.end()) {
                return false;
            }
            found->second.flags = flags;
            if (found->second.watch && me->m_loop != nullptr) {
                std::uint32_t events = 0;
                events |= (flags & CLAP_POSIX_FD_READ) != 0 ? std::uint32_t{MainLoop::Readable} : 0U;
                events |= (flags & CLAP_POSIX_FD_WRITE) != 0 ? std::uint32_t{MainLoop::Writable} : 0U;
                return me->m_loop->changeFd(*found->second.watch, events);
            }
            return true;
        },
        [](const clap_host *host, int fd) {
            ClapHost *me = self(host);
            const auto found = me->m_fds.find(fd);
            if (found == me->m_fds.end()) {
                return false;
            }
            if (found->second.watch && me->m_loop != nullptr) {
                me->m_loop->remove(*found->second.watch);
            }
            me->m_fds.erase(found);
            return true;
        }};
    static const clap_host_preset_load presetLoad{
        [](const clap_host *, std::uint32_t, const char *location, const char *, std::int32_t, const char *message) {
            std::fprintf(stderr,
                         "plugin: a preset at %s was not put in: %s\n",
                         location != nullptr ? location : "(the plugin)",
                         message != nullptr ? message : "");
        },
        [](const clap_host *host, std::uint32_t kind, const char *location, const char *key) {
            ClapHost *me = self(host);
            const std::lock_guard lock(me->m_loadedLock);
            me->m_loaded = std::to_string(kind) + "\n" + (location != nullptr ? location : "") + "\n"
                + (key != nullptr ? key : "");
        }};
    const std::string_view wanted{id};
    if (wanted == CLAP_EXT_LOG) {
        return &log;
    }
    if (wanted == CLAP_EXT_THREAD_CHECK) {
        return &threads;
    }
    if (wanted == CLAP_EXT_PARAMS) {
        return &params;
    }
    if (wanted == CLAP_EXT_LATENCY) {
        return &latency;
    }
    if (wanted == CLAP_EXT_STATE) {
        return &state;
    }
    if (wanted == CLAP_EXT_AUDIO_PORTS) {
        return &ports;
    }
    if (wanted == CLAP_EXT_GUI) {
        return &gui;
    }
    if (wanted == CLAP_EXT_TIMER_SUPPORT) {
        return &timers;
    }
    if (wanted == CLAP_EXT_POSIX_FD_SUPPORT) {
        return &fds;
    }
    if (wanted == CLAP_EXT_PRESET_LOAD || wanted == CLAP_EXT_PRESET_LOAD_COMPAT) {
        return &presetLoad;
    }
    return nullptr;
}

// --- describing ---

std::vector<plugin::PluginParam>
clapParams(const clap_plugin *plugin, std::vector<clap_id> *ids, std::vector<void *> *cookies)
{
    std::vector<plugin::PluginParam> found;
    const auto *params = static_cast<const clap_plugin_params *>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
    if (params == nullptr) {
        return found;
    }
    const std::uint32_t count = params->count(plugin);
    for (std::uint32_t index = 0; index < count; ++index) {
        clap_param_info info{};
        if (!params->get_info(plugin, index, &info)
            || (info.flags & (CLAP_PARAM_IS_HIDDEN | CLAP_PARAM_IS_READONLY)) != 0) {
            continue;
        }
        plugin::PluginParam param;
        param.id = std::to_string(info.id);
        param.name = info.module[0] != '\0' ? std::string{info.module} + "/" + info.name : std::string{info.name};
        param.minimum = static_cast<float>(std::min(info.min_value, info.max_value));
        param.maximum = static_cast<float>(std::max(info.min_value, info.max_value));
        param.defaultValue = std::clamp(static_cast<float>(info.default_value), param.minimum, param.maximum);
        if ((info.flags & CLAP_PARAM_IS_STEPPED) != 0) {
            describeSteps(plugin, params, info, param);
        }
        found.push_back(std::move(param));
        if (ids != nullptr) {
            ids->push_back(info.id);
        }
        if (cookies != nullptr) {
            cookies->push_back(info.cookie);
        }
    }
    return found;
}

bool describeClap(const std::string &path, const std::function<void(const plugin::PluginDescription &)> &found)
{
    const std::shared_ptr<ClapLibrary> library = ClapLibrary::open(path);
    if (!library) {
        return false;
    }
    ClapHost host;
    // **Each plugin is let go once the next is made**: a file's plugins may
    // share what the last one alive keeps running. LSP's share a worker
    // thread that takes some 50 ms to stop, and letting each go first
    // stopped and started it again for every plugin: 6.5 s for its 194.
    const clap_plugin *previous = nullptr;
    const std::uint32_t count = library->count();
    for (std::uint32_t index = 0; index < count; ++index) {
        const clap_plugin_descriptor *descriptor = library->descriptorAt(index);
        if (descriptor == nullptr || descriptor->id == nullptr) {
            continue;
        }
        plugin::PluginDescription description;
        description.format = "clap";
        description.id = descriptor->id;
        description.name = descriptor->name != nullptr ? descriptor->name : descriptor->id;
        description.vendor = descriptor->vendor != nullptr ? descriptor->vendor : "";
        description.category = categoryOf(descriptor);
        const clap_plugin *plugin = library->create(host.host(), descriptor->id);
        if (previous != nullptr) {
            previous->destroy(previous);
            previous = nullptr;
        }
        if (plugin == nullptr) {
            // One that will not start is not offered: its block could not
            // run it.
            std::fprintf(stderr, "the plugin %s would not start\n", descriptor->id);
            continue;
        }
        description.audioInputs = sum(portsOf(plugin, true));
        description.audioOutputs = sum(portsOf(plugin, false));
        description.params = clapParams(plugin);
        description.parameters = static_cast<std::uint32_t>(description.params.size());
        previous = plugin;
        found(description);
    }
    if (previous != nullptr) {
        previous->destroy(previous);
    }
    return true;
}

// --- ClapPlugin ---

std::unique_ptr<ClapPlugin>
ClapPlugin::make(const std::shared_ptr<ClapLibrary> &library, const std::string &id, Mono mono)
{
    std::unique_ptr<ClapPlugin> made(new ClapPlugin(library, id, mono));
    if (made->m_instances.empty()) {
        return nullptr;
    }
    return made;
}

ClapPlugin::ClapPlugin(std::shared_ptr<ClapLibrary> library, std::string id, Mono mono)
    : m_library(std::move(library))
    , m_id(std::move(id))
    , m_mono(mono)
{
    m_hosts.push_back(std::make_unique<ClapHost>());
    const clap_plugin *first = m_library->create(m_hosts.front()->host(), m_id);
    if (first == nullptr) {
        return;
    }
    m_hosts.front()->setPlugin(first);
    hookWindow(*m_hosts.front());
    m_instances.push_back(first);
    m_name = first->desc != nullptr && first->desc->name != nullptr ? first->desc->name : m_id;
    m_params = clapParams(first, &m_ids, &m_cookies);
    m_values = std::vector<std::atomic<float>>(m_params.size());
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        m_choices.push_back(choiceValues(m_params[index]));
        m_values[index].store(m_params[index].defaultValue);
        m_byId.emplace_back(m_ids[index], index);
        dsp::ParamDescriptor descriptor;
        descriptor.id = m_params[index].id;
        descriptor.type = dsp::ParamType::Float;
        descriptor.defaultValue = dsp::ParamValue::ofFloat(m_params[index].defaultValue);
        m_descriptors.push_back(std::move(descriptor));
    }
    std::ranges::sort(m_byId);
    m_changed.assign(m_params.size(), 0);
    m_changedList.assign(m_params.size(), 0);
    m_events.resize(m_params.size());

    m_inputPorts = portsOf(first, true);
    m_outputPorts = portsOf(first, false);
    m_audioIns = sum(m_inputPorts);
    m_audioOuts = sum(m_outputPorts);
    m_info.typeId = "clap";
    m_info.category = dsp::BlockCategory::Plugins;
    m_info.inputs = numbered(m_audioIns == 1 ? 2 : m_audioIns, "in_");
    m_info.outputs = numbered(m_audioOuts == 1 ? 2 : m_audioOuts, "out_");
}

ClapPlugin::~ClapPlugin()
{
    closeUi();
    if (m_tick && loop() != nullptr) {
        loop()->remove(*m_tick);
    }
    freeInstances();
    // The hosts after their instances: a plugin lets go of its timers as it
    // goes.
    m_hosts.clear();
}

void ClapPlugin::freeInstances()
{
    for (std::size_t which = 0; which < m_instances.size(); ++which) {
        const clap_plugin *instance = m_instances[which];
        if (which < m_processing.size() && m_processing[which] != 0) {
            // The audio thread has let go of this plugin (the host swaps a
            // block out between cycles), so this thread stands in for it.
            const ClapHost::ActingAsAudio acting;
            instance->stop_processing(instance);
        }
        if (m_active) {
            instance->deactivate(instance);
        }
        instance->destroy(instance);
    }
    m_instances.clear();
    m_processing.clear();
    m_active = false;
}

std::expected<void, dsp::BlockError> ClapPlugin::prepare(const dsp::PrepareSpec &spec)
{
    if (spec.sampleRate <= 0.0 || spec.maxBlockFrames == 0 || spec.maxBlockFrames > dsp::kMaxBlockFrames
        || m_instances.empty()) {
        return std::unexpected(dsp::BlockError::UnsupportedSpec);
    }
    // Once, like the LV2 host: a plugin at another rate is another block.
    if (m_active) {
        return std::unexpected(dsp::BlockError::UnsupportedSpec);
    }
    // **Twice for a mono plugin on a stereo lane, once a side**, unless it
    // is to hear the two sides mixed — or makes more than one side itself.
    if (m_audioIns == 1 && m_audioOuts <= 1 && m_mono == Mono::DualMono) {
        m_hosts.push_back(std::make_unique<ClapHost>());
        const clap_plugin *second = m_library->create(m_hosts.back()->host(), m_id);
        if (second == nullptr) {
            m_hosts.pop_back();
            return std::unexpected(dsp::BlockError::UnsupportedSpec);
        }
        m_hosts.back()->setPlugin(second);
        if (loop() != nullptr) {
            m_hosts.back()->setLoop(loop());
        }
        m_instances.push_back(second);
    }
    flushValues();
    for (const clap_plugin *instance : m_instances) {
        if (!instance->activate(instance, spec.sampleRate, 1, spec.maxBlockFrames)) {
            // The ones already active are let go with the rest.
            for (const clap_plugin *started : m_instances) {
                if (started == instance) {
                    break;
                }
                started->deactivate(started);
            }
            return std::unexpected(dsp::BlockError::UnsupportedSpec);
        }
    }
    m_active = true;
    m_processing.assign(m_instances.size(), 0);

    const std::size_t count = m_instances.size();
    m_inBuffers.assign(count, std::vector<clap_audio_buffer>(m_inputPorts.size()));
    m_outBuffers.assign(count, std::vector<clap_audio_buffer>(m_outputPorts.size()));
    m_inChannels.assign(count, std::vector<float *>(std::max<std::uint32_t>(m_audioIns, 1), nullptr));
    m_outChannels.assign(count, std::vector<float *>(std::max<std::uint32_t>(m_audioOuts, 1), nullptr));
    for (std::size_t which = 0; which < count; ++which) {
        std::uint32_t at = 0;
        for (std::size_t port = 0; port < m_inputPorts.size(); ++port) {
            clap_audio_buffer &buffer = m_inBuffers[which][port];
            buffer.data32 = m_inChannels[which].data() + at;
            buffer.channel_count = m_inputPorts[port];
            at += m_inputPorts[port];
        }
        at = 0;
        for (std::size_t port = 0; port < m_outputPorts.size(); ++port) {
            clap_audio_buffer &buffer = m_outBuffers[which][port];
            buffer.data32 = m_outChannels[which].data() + at;
            buffer.channel_count = m_outputPorts[port];
            at += m_outputPorts[port];
        }
    }
    m_silence.assign(spec.maxBlockFrames, 0.0f);
    m_mix.assign(spec.maxBlockFrames, 0.0f);
    m_spare.assign(spec.maxBlockFrames, 0.0f);
    refreshLatency();
    if (!m_pendingState.empty()) {
        const std::string pending = std::exchange(m_pendingState, {});
        (void)loadStateText(pending);
    }
    return {};
}

void ClapPlugin::flushValues()
{
    const auto *params = static_cast<const clap_plugin_params *>(
        m_instances.front()->get_extension(m_instances.front(), CLAP_EXT_PARAMS));
    if (params == nullptr || m_params.empty()) {
        return;
    }
    std::vector<clap_event_param_value> changes;
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        clap_event_param_value change{};
        change.header = {sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        change.param_id = m_ids[index];
        change.cookie = m_cookies[index];
        change.note_id = -1;
        change.port_index = -1;
        change.channel = -1;
        change.key = -1;
        change.value = m_values[index].load(std::memory_order_relaxed);
        changes.push_back(change);
    }
    InputEvents in;
    in.items = &changes;
    in.count = static_cast<std::uint32_t>(changes.size());
    in.events.ctx = &in;
    in.events.size = &sizeOf;
    in.events.get = &eventAt;
    OutputEvents out;
    out.byId = &m_byId;
    out.values = &m_values;
    out.events.ctx = &out;
    out.events.try_push = &pushed;
    for (const clap_plugin *instance : m_instances) {
        const auto *theirs =
            static_cast<const clap_plugin_params *>(instance->get_extension(instance, CLAP_EXT_PARAMS));
        theirs->flush(instance, &in.events, &out.events);
    }
}

void ClapPlugin::process(dsp::ProcessContext &context) noexcept RB_NONBLOCKING
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
    const bool downmix = m_audioIns == 1 && !dual;
    if (downmix) {
        const float *left = inputOf(0);
        const float *right = inputOf(1);
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            m_mix[frame] = 0.5f * (left[frame] + right[frame]);
        }
    }

    // The changes since the last cycle, as events at its first sample.
    m_eventCount = 0;
    for (std::uint32_t at = 0; at < m_changedCount; ++at) {
        const std::uint32_t index = m_changedList[at];
        m_changed[index] = 0;
        clap_event_param_value &change = m_events[m_eventCount++];
        change.header = {sizeof(clap_event_param_value), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        change.param_id = m_ids[index];
        change.cookie = m_cookies[index];
        change.note_id = -1;
        change.port_index = -1;
        change.channel = -1;
        change.key = -1;
        change.value = m_values[index].load(std::memory_order_relaxed);
    }
    m_changedCount = 0;
    InputEvents in;
    in.items = &m_events;
    in.count = m_eventCount;
    in.events.ctx = &in;
    in.events.size = &sizeOf;
    in.events.get = &eventAt;
    OutputEvents out;
    out.byId = &m_byId;
    out.values = &m_values;
    out.events.ctx = &out;
    out.events.try_push = &pushed;

    for (std::size_t which = 0; which < m_instances.size(); ++which) {
        const clap_plugin *instance = m_instances[which];
        if (m_processing[which] == 0) {
            if (!startProcessingOf(instance)) {
                silenceOutputs();
                return;
            }
            m_processing[which] = 1;
        }
        std::vector<float *> &ins = m_inChannels[which];
        std::vector<float *> &outs = m_outChannels[which];
        for (std::uint32_t channel = 0; channel < m_audioIns; ++channel) {
            ins[channel] = dual ? inputOf(static_cast<std::uint32_t>(which))
                : downmix       ? m_mix.data()
                                : inputOf(channel);
        }
        for (std::uint32_t channel = 0; channel < m_audioOuts; ++channel) {
            outs[channel] = dual ? outputOf(static_cast<std::uint32_t>(which)) : outputOf(channel);
        }
        clap_process process{};
        process.steady_time = m_steadyTime;
        process.frames_count = frames;
        process.transport = nullptr;
        process.audio_inputs = m_inBuffers[which].data();
        process.audio_outputs = m_outBuffers[which].data();
        process.audio_inputs_count = static_cast<std::uint32_t>(m_inBuffers[which].size());
        process.audio_outputs_count = static_cast<std::uint32_t>(m_outBuffers[which].size());
        process.in_events = &in.events;
        process.out_events = &out.events;
        // **What the first instance's window does**, for the main thread
        // while it listens, and for the other instance of a mono plugin
        // run twice.
        const bool first = which == 0;
        out.said = first && m_windowListens.load(std::memory_order_acquire) ? &m_said : nullptr;
        out.changed = first && dual ? &m_changed : nullptr;
        out.changedList = &m_changedList;
        out.changedCount = &m_changedCount;
        if (processOf(instance, &process) == CLAP_PROCESS_ERROR) {
            silenceOutputs();
            return;
        }
    }
    m_steadyTime += frames;

    // A mono output goes to both sides.
    if (m_audioOuts == 1 && !dual && context.output.channelCount() > 1) {
        const std::span<float> left = context.output.channel(0);
        const std::span<float> right = context.output.channel(1);
        if (!left.empty() && !right.empty()) {
            std::copy_n(left.begin(), frames, right.begin());
        }
    }
}

void ClapPlugin::reset() noexcept RB_NONBLOCKING { }

void ClapPlugin::applyParam(std::uint32_t index, float value) noexcept RB_NONBLOCKING
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

void ClapPlugin::remember(std::uint32_t index, float value) noexcept
{
    m_values[index].store(value, std::memory_order_relaxed);
}

std::expected<void, dsp::BlockError> ClapPlugin::setParam(std::string_view id, const dsp::ParamValue &value)
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            // The value itself, as a state holds it; it reaches the plugin
            // when it starts. Once it runs, a change comes through
            // applyParam().
            remember(index, static_cast<float>(value.asNumber().value_or(0.0)));
            return {};
        }
    }
    return std::unexpected(dsp::BlockError::UnknownParam);
}

dsp::ParamValue ClapPlugin::param(std::string_view id) const
{
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        if (m_params[index].id == id) {
            return dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed));
        }
    }
    return {};
}

dsp::ParamValueMap ClapPlugin::saveState() const
{
    dsp::ParamValueMap state;
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        state.emplace(m_params[index].id, dsp::ParamValue::ofFloat(m_values[index].load(std::memory_order_relaxed)));
    }
    return state;
}

std::expected<void, dsp::BlockError> ClapPlugin::loadState(const dsp::ParamValueMap &state)
{
    for (const auto &[id, value] : state) {
        (void)setParam(id, value);
    }
    return {};
}

std::string ClapPlugin::stateText() const
{
    if (!m_active) {
        return m_pendingState;
    }
    const clap_plugin *first = m_instances.front();
    const auto *state = static_cast<const clap_plugin_state *>(first->get_extension(first, CLAP_EXT_STATE));
    if (state == nullptr) {
        return {};
    }
    std::string bytes;
    clap_ostream stream{};
    stream.ctx = &bytes;
    stream.write = [](const clap_ostream *out, const void *buffer, std::uint64_t size) -> std::int64_t {
        static_cast<std::string *>(out->ctx)->append(static_cast<const char *>(buffer), size);
        return static_cast<std::int64_t>(size);
    };
    if (!state->save(first, &stream)) {
        return {};
    }
    return toBase64(bytes);
}

bool ClapPlugin::loadStateText(const std::string &text)
{
    if (!m_active) {
        m_pendingState = text;
        return true;
    }
    const std::optional<std::string> bytes = fromBase64(text);
    if (!bytes) {
        return false;
    }
    struct Reading {
        const std::string *bytes;
        std::size_t at;
    };
    // Every instance: a mono plugin run twice is one plugin to the person.
    for (const clap_plugin *instance : m_instances) {
        const auto *state = static_cast<const clap_plugin_state *>(instance->get_extension(instance, CLAP_EXT_STATE));
        if (state == nullptr) {
            return false;
        }
        Reading reading{&*bytes, 0};
        clap_istream stream{};
        stream.ctx = &reading;
        stream.read = [](const clap_istream *in, void *buffer, std::uint64_t size) -> std::int64_t {
            auto *from = static_cast<Reading *>(in->ctx);
            const std::size_t left = from->bytes->size() - from->at;
            const std::size_t taken = std::min<std::size_t>(left, size);
            std::memcpy(buffer, from->bytes->data() + from->at, taken);
            from->at += taken;
            return static_cast<std::int64_t>(taken);
        };
        if (!state->load(instance, &stream)) {
            return false;
        }
    }
    readValues();
    return true;
}

void ClapPlugin::readValues()
{
    const clap_plugin *first = m_instances.front();
    const auto *params = static_cast<const clap_plugin_params *>(first->get_extension(first, CLAP_EXT_PARAMS));
    if (params == nullptr) {
        return;
    }
    for (std::uint32_t index = 0; index < m_params.size(); ++index) {
        double value = 0.0;
        if (params->get_value(first, m_ids[index], &value)) {
            remember(index, static_cast<float>(value));
        }
    }
}

void ClapPlugin::refreshLatency() noexcept
{
    if (!m_active) {
        return;
    }
    const clap_plugin *first = m_instances.front();
    const auto *latency = static_cast<const clap_plugin_latency *>(first->get_extension(first, CLAP_EXT_LATENCY));
    m_info.latencySamples = latency != nullptr ? latency->get(first) : 0U;
}

bool ClapPlugin::idle()
{
    bool restart = false;
    for (std::size_t which = 0; which < m_hosts.size() && which < m_instances.size(); ++which) {
        if (m_hosts[which]->takeCallback()) {
            m_instances[which]->on_main_thread(m_instances[which]);
        }
        restart = m_hosts[which]->takeRestart() || restart;
    }
    return restart;
}

// --- the plugin's own window ---

void ClapPlugin::onAttached()
{
    for (const std::unique_ptr<ClapHost> &host : m_hosts) {
        host->setLoop(loop());
        // What the plugin said while it was being made is not a person's
        // doing: a state loaded then is the engine's.
        (void)host->takeRescan();
        (void)host->takeLoaded();
    }
    if (!m_tick && loop() != nullptr) {
        m_tick = loop()->addTimer(std::chrono::milliseconds{33}, [this] { editorTick(); });
    }
}

void ClapPlugin::hookWindow(ClapHost &host)
{
    host.requestResize = [this](std::uint32_t width, std::uint32_t height) {
        if (!m_window) {
            return false; // a floating window resizes itself
        }
        m_window->resize(static_cast<int>(width), static_cast<int>(height));
        return true;
    };
    host.requestShow = [this](bool show) {
        if (m_gui == nullptr) {
            return false;
        }
        const clap_plugin *first = m_instances.front();
        return show ? m_gui->show(first) : m_gui->hide(first);
    };
    host.resizeHintsChanged = [this] {
        if (m_window && m_gui != nullptr) {
            m_window->setResizable(m_gui->can_resize(m_instances.front()));
        }
    };
}

bool ClapPlugin::hasUi() const
{
    if (m_instances.empty()) {
        return false;
    }
    const clap_plugin *first = m_instances.front();
    const auto *gui = static_cast<const clap_plugin_gui *>(first->get_extension(first, CLAP_EXT_GUI));
    if (gui == nullptr) {
        return false;
    }
    const char *wayland = std::getenv("WAYLAND_DISPLAY");
    return gui->is_api_supported(first, CLAP_WINDOW_API_X11, true)
        || (wayland != nullptr && *wayland != '\0' && gui->is_api_supported(first, CLAP_WINDOW_API_WAYLAND, true))
        || (haveDisplay() && gui->is_api_supported(first, CLAP_WINDOW_API_X11, false));
}

Editable::Opened ClapPlugin::openUi()
{
    if (m_gui != nullptr) {
        return Opened::Open;
    }
    if (!hasUi() || loop() == nullptr) {
        return Opened::Unavailable;
    }
    const clap_plugin *first = m_instances.front();
    const auto *gui = static_cast<const clap_plugin_gui *>(first->get_extension(first, CLAP_EXT_GUI));
    // **Which window**: one the host makes, which opens where it was last,
    // when there is a display and the plugin can be put in one; otherwise
    // the plugin's own floating window, Wayland's where it offers one.
    const char *wayland = std::getenv("WAYLAND_DISPLAY");
    const bool embedded = haveDisplay() && gui->is_api_supported(first, CLAP_WINDOW_API_X11, false);
    const bool waylandFloating = !embedded && wayland != nullptr && *wayland != '\0'
        && gui->is_api_supported(first, CLAP_WINDOW_API_WAYLAND, true);
    const char *api = waylandFloating ? CLAP_WINDOW_API_WAYLAND : CLAP_WINDOW_API_X11;
    if (!gui->create(first, api, !embedded)) {
        return Opened::Failed;
    }
    m_gui = gui;
    m_floating = !embedded;
    ClapSaid stale;
    while (m_said.tryPop(stale)) { }
    m_windowListens.store(true, std::memory_order_release);
    if (embedded) {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        (void)gui->get_size(first, &width, &height);
        const std::string key = "clap " + m_id;
        m_window = X11Window::open(
            *loop(),
            m_name,
            key,
            static_cast<int>(width),
            static_cast<int>(height),
            [this](bool failed) {
                closeUi();
                if (events() != nullptr && events()->closed) {
                    events()->closed(failed ? "failed" : "closed");
                }
            },
            [this](int newWidth, int newHeight) {
                if (m_gui == nullptr || !m_gui->can_resize(m_instances.front())) {
                    return;
                }
                auto w = static_cast<std::uint32_t>(newWidth);
                auto h = static_cast<std::uint32_t>(newHeight);
                if (m_gui->adjust_size(m_instances.front(), &w, &h)) {
                    (void)m_gui->set_size(m_instances.front(), w, h);
                }
            });
        if (!m_window) {
            gui->destroy(first);
            m_gui = nullptr;
            m_windowListens.store(false, std::memory_order_release);
            return Opened::Unavailable;
        }
        const bool resizable = gui->can_resize(first);
        m_window->setResizable(resizable);
        if (const std::optional<WindowPlace> kept = placeOf(key); kept && resizable) {
            // Its size from last time, as near as the plugin takes it.
            auto w = static_cast<std::uint32_t>(kept->width);
            auto h = static_cast<std::uint32_t>(kept->height);
            if (gui->adjust_size(first, &w, &h) && gui->set_size(first, w, h)) {
                m_window->resize(static_cast<int>(w), static_cast<int>(h));
            }
        } else if (width > 0 && height > 0) {
            m_window->resize(static_cast<int>(width), static_cast<int>(height));
        }
        clap_window parent{};
        parent.api = CLAP_WINDOW_API_X11;
        parent.x11 = m_window->id();
        if (!gui->set_parent(first, &parent)) {
            closeUi();
            return Opened::Failed;
        }
        m_window->show();
    } else {
        gui->suggest_title(first, m_name.c_str());
    }
    if (!gui->show(first)) {
        closeUi();
        return Opened::Failed;
    }
    return Opened::Open;
}

void ClapPlugin::closeUi()
{
    m_windowListens.store(false, std::memory_order_release);
    if (m_gui != nullptr) {
        const clap_plugin *first = m_instances.front();
        (void)m_gui->hide(first);
        m_gui->destroy(first);
        m_gui = nullptr;
    }
    // The plugin's window first, then the one it was in.
    m_window.reset();
}

void ClapPlugin::editorTick()
{
    if (m_instances.empty()) {
        return;
    }
    // **What a person did in the window**: values, and the hold and the
    // letting go round them.
    ClapSaid said;
    while (m_said.tryPop(said)) {
        if (events() == nullptr || m_gui == nullptr) {
            continue;
        }
        if (said.kind == ClapSaid::Value && events()->changed) {
            events()->changed(said.index, said.value);
        } else if (said.kind != ClapSaid::Value && events()->gesture) {
            events()->gesture(said.index, said.kind == ClapSaid::Begin);
        }
    }
    // **A floating window a person closed**: taken down here, not inside
    // the plugin's own call that said so.
    bool closed = false;
    bool rescanned = false;
    std::optional<std::string> loaded;
    for (const std::unique_ptr<ClapHost> &host : m_hosts) {
        closed = host->takeClosed() || closed;
        rescanned = host->takeRescan() || rescanned;
        if (std::optional<std::string> said = host->takeLoaded(); said && !loaded) {
            loaded = std::move(said);
        }
    }
    if (closed && m_gui != nullptr) {
        closeUi();
        if (events() != nullptr && events()->closed) {
            events()->closed("closed");
        }
    }
    // **Values the plugin changed itself**: a preset asked for here, or one
    // chosen in its own window. Read once they are in: when it says so, or
    // a few turns after it was asked, for a plugin that does not say.
    const bool waited = m_loading && ++m_loadingFor >= 3;
    if (rescanned || loaded || waited) {
        readValues();
        std::string shown;
        if (loaded) {
            const std::size_t first = loaded->find('\n');
            const std::size_t second = loaded->find('\n', first + 1);
            shown = presetId(static_cast<std::uint32_t>(std::strtoul(loaded->c_str(), nullptr, 10)),
                             loaded->substr(first + 1, second - first - 1),
                             loaded->substr(second + 1));
        } else if (m_loading) {
            shown = *m_loading;
        }
        const bool ours = m_loading.has_value();
        m_loading.reset();
        m_loadingFor = 0;
        if (events() != nullptr) {
            if ((ours || m_gui != nullptr) && events()->valuesChanged) {
                events()->valuesChanged(true);
            }
            if (!shown.empty() && events()->presetShown) {
                events()->presetShown(shown);
            }
        }
    }
}

// --- presets ---

std::string ClapPlugin::presetId(std::uint32_t kind, const std::string &location, const std::string &key)
{
    // **Readable, and the same on every listing**: where the preset is,
    // and which of the presets there.
    std::string id = kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN ? std::string{"plugin:"} : location;
    if (!key.empty()) {
        id += "#" + key;
    }
    return id;
}

namespace
{

/// What the preset discovery says while one provider is asked.
struct Discovery {
    std::string pluginId;
    std::vector<std::string> extensions;
    std::vector<clap_preset_discovery_location> locations;
    std::vector<std::string> locationPaths;
    /// The presets of the file or location being read now, and whether each
    /// is for this plugin.
    struct Preset {
        std::string name;
        std::string key;
        bool ours = false;
    };
    std::vector<Preset> presets;
};

} // namespace

std::vector<plugin::PresetEntry> ClapPlugin::presets()
{
    std::vector<plugin::PresetEntry> listed;
    m_found.clear();
    const clap_preset_discovery_factory *factory = m_library->presetFactory();
    if (factory == nullptr) {
        return listed;
    }
    for (std::uint32_t index = 0; index < factory->count(factory); ++index) {
        const clap_preset_discovery_provider_descriptor *descriptor = factory->get_descriptor(factory, index);
        if (descriptor == nullptr || descriptor->id == nullptr) {
            continue;
        }
        Discovery discovery;
        discovery.pluginId = m_id;
        clap_preset_discovery_indexer indexer{};
        indexer.clap_version = CLAP_VERSION;
        indexer.name = "Reverblabr";
        indexer.vendor = "Reverblabr";
        indexer.url = "";
        indexer.version = "1";
        indexer.indexer_data = &discovery;
        indexer.declare_filetype = [](const clap_preset_discovery_indexer *self,
                                      const clap_preset_discovery_filetype *filetype) {
            auto *found = static_cast<Discovery *>(self->indexer_data);
            found->extensions.emplace_back(filetype->file_extension != nullptr ? filetype->file_extension : "");
            return true;
        };
        indexer.declare_location = [](const clap_preset_discovery_indexer *self,
                                      const clap_preset_discovery_location *location) {
            auto *found = static_cast<Discovery *>(self->indexer_data);
            found->locations.push_back(*location);
            found->locationPaths.emplace_back(location->location != nullptr ? location->location : "");
            return true;
        };
        indexer.declare_soundpack = [](const clap_preset_discovery_indexer *, const clap_preset_discovery_soundpack *) {
            return true;
        };
        indexer.get_extension = [](const clap_preset_discovery_indexer *, const char *) -> const void * {
            return nullptr;
        };
        const clap_preset_discovery_provider *provider = factory->create(factory, &indexer, descriptor->id);
        if (provider == nullptr) {
            continue;
        }
        if (!provider->init(provider)) {
            provider->destroy(provider);
            continue;
        }
        clap_preset_discovery_metadata_receiver receiver{};
        receiver.receiver_data = &discovery;
        receiver.on_error = [](const clap_preset_discovery_metadata_receiver *, std::int32_t, const char *) { };
        receiver.begin_preset = [](const clap_preset_discovery_metadata_receiver *self,
                                   const char *name,
                                   const char *key) {
            auto *found = static_cast<Discovery *>(self->receiver_data);
            found->presets.push_back(Discovery::Preset{name != nullptr ? name : "", key != nullptr ? key : "", false});
            return true;
        };
        receiver.add_plugin_id = [](const clap_preset_discovery_metadata_receiver *self,
                                    const clap_universal_plugin_id *pluginId) {
            auto *found = static_cast<Discovery *>(self->receiver_data);
            if (!found->presets.empty() && pluginId != nullptr && pluginId->abi != nullptr && pluginId->id != nullptr
                && std::string_view{pluginId->abi} == "clap" && pluginId->id == found->pluginId) {
                found->presets.back().ours = true;
            }
        };
        receiver.set_soundpack_id = [](const clap_preset_discovery_metadata_receiver *, const char *) { };
        receiver.set_flags = [](const clap_preset_discovery_metadata_receiver *, std::uint32_t) { };
        receiver.add_creator = [](const clap_preset_discovery_metadata_receiver *, const char *) { };
        receiver.set_description = [](const clap_preset_discovery_metadata_receiver *, const char *) { };
        receiver.set_timestamps = [](const clap_preset_discovery_metadata_receiver *, clap_timestamp, clap_timestamp) {
        };
        receiver.add_feature = [](const clap_preset_discovery_metadata_receiver *, const char *) { };
        receiver.add_extra_info = [](const clap_preset_discovery_metadata_receiver *, const char *, const char *) { };

        const auto read = [&](std::uint32_t kind, const std::string &location) {
            discovery.presets.clear();
            if (!provider->get_metadata(provider,
                                        kind,
                                        kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN ? nullptr : location.c_str(),
                                        &receiver)) {
                return;
            }
            for (const Discovery::Preset &preset : discovery.presets) {
                if (!preset.ours) {
                    continue;
                }
                const std::string id = presetId(kind, location, preset.key);
                if (m_found.emplace(id, Found{kind, location, preset.key}).second) {
                    listed.push_back(plugin::PresetEntry{id, preset.name.empty() ? id : preset.name});
                }
            }
        };
        // **A file location is a file or a folder of them**, the ones of the
        // types the provider declared; the plugin's own are asked of it.
        for (std::size_t at = 0; at < discovery.locations.size(); ++at) {
            const clap_preset_discovery_location &location = discovery.locations[at];
            const std::string &path = discovery.locationPaths[at];
            if (location.kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN) {
                read(location.kind, {});
                continue;
            }
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error)) {
                read(location.kind, path);
                continue;
            }
            for (std::filesystem::recursive_directory_iterator walk(
                     path, std::filesystem::directory_options::skip_permission_denied, error);
                 !error && walk != std::filesystem::recursive_directory_iterator();
                 walk.increment(error)) {
                if (!walk->is_regular_file(error)) {
                    continue;
                }
                const std::string file = walk->path().string();
                const bool wanted = discovery.extensions.empty()
                    || std::ranges::any_of(discovery.extensions, [&file](const std::string &extension) {
                                        return extension.empty() || file.ends_with("." + extension);
                                    });
                if (wanted) {
                    read(location.kind, file);
                }
            }
        }
        provider->destroy(provider);
    }
    std::ranges::sort(listed, [](const plugin::PresetEntry &left, const plugin::PresetEntry &right) {
        return left.name != right.name ? left.name < right.name : left.id < right.id;
    });
    return listed;
}

bool ClapPlugin::loadPreset(const std::string &id)
{
    if (!m_found.contains(id)) {
        (void)presets(); // a preset listed by an earlier host
    }
    const auto found = m_found.find(id);
    if (found == m_found.end() || !m_active) {
        return false;
    }
    // Every instance: a mono plugin run twice is one plugin to the person.
    for (const clap_plugin *instance : m_instances) {
        const auto *load =
            static_cast<const clap_plugin_preset_load *>(instance->get_extension(instance, CLAP_EXT_PRESET_LOAD));
        if (load == nullptr) {
            load = static_cast<const clap_plugin_preset_load *>(
                instance->get_extension(instance, CLAP_EXT_PRESET_LOAD_COMPAT));
        }
        if (load == nullptr
            || !load->from_location(
                instance,
                found->second.kind,
                found->second.kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN ? nullptr : found->second.location.c_str(),
                found->second.key.empty() ? nullptr : found->second.key.c_str())) {
            return false;
        }
    }
    // Its values are read once it says they are in (editorTick()).
    m_loading = id;
    m_loadingFor = 0;
    return true;
}

} // namespace rb::pluginhost
